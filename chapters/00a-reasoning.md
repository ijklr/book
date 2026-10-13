# A method for reasoning from first principles

Suppose someone asks you to build a fast message queue. It is tempting to reach for a familiar implementation, or to search for a clever one. Begin earlier: what does fast mean, which messages must survive, who produces and consumes them, and what happens when the consumer cannot keep up?

First-principles reasoning means stating what you know and deriving what follows. In this book, the starting points include requirements, mathematical facts, language rules, operating-system contracts, and explicit model assumptions. Use them to compare designs and identify what still needs testing.

In a 2013 interview, Elon Musk describes first-principles reasoning as starting with fundamental truths and building conclusions from them. He also explains that the effort makes it worth applying selectively to important problems. That is the habit we will practice at consequential design decisions throughout this book. [Elon Musk interviewed by Alaina G. Levine, APS News](https://www.aps.org/apsnews/2013/10/profiles-in-versatility)

The derivations below apply that habit to systems programming. Their justification comes from the contracts, arguments, and evidence you can inspect.

## Start at a useful boundary

You cannot derive a particular POSIX function signature from physics. People designed that interface. Nor can an experiment showing that invalid C code happened to work replace the language's lifetime rules. Good reasoning accepts a verified contract at the appropriate level and reasons carefully above it.

Choose a boundary that serves the problem. While designing a copier, take the documented behavior of `read` and `write` as inputs. While investigating an unexpected syscall cost, look below that boundary. While choosing a network protocol, the byte-stream contract matters; the precise voltage on a cable usually does not.

You can always ask another “why.” Useful stopping points are a relevant contract, a demonstrated mathematical consequence, or an explicitly uncertain assumption with a plan to investigate it. Writing a familiar term such as “the kernel handles it” is not a stopping point if the unresolved behavior is exactly what your design depends on.

## Keep a ledger of claims

Classify a claim before relying on it. The categories below prevent an observation from silently turning into a guarantee.

| Kind of claim | Example | How to challenge or establish it |
| --- | --- | --- |
| Requirement | A successful copy must preserve every input byte in order | Ask the caller what success must mean |
| External contract | A successful write can report less progress than requested | Read the relevant interface specification and platform notes |
| Model assumption | The file is not modified during this scan | State who can ensure it, or design for changes |
| Mathematical consequence | A finite buffer cannot hold an indefinitely growing backlog | State the model and follow its accounting |
| Observation | This run made three positive reads | Record the input, environment, and trace |
| Design choice | Use an 8192-byte input buffer | Compare consequences under the workload |

Requirements can change, models can be incomplete, and implementations can have bugs. Labeling a claim does not protect it from criticism; it tells you what kind of criticism is relevant. A benchmark can compare buffer sizes. It cannot establish that a caller will never truncate a mapped file.

For an important assumption, record the consequence if it is false. “There is one producer” is incomplete documentation for a concurrent structure. Add which state transitions would conflict if a second producer appeared and how the interface enforces or exposes that restriction.

## Question a requirement that already names the solution

“Make this queue ten times larger” sounds like a requirement. It may actually be a proposed remedy for lost records. Ask what outcome the caller needs: tolerate a finite burst, reduce producer waiting, or guarantee delivery during an outage? Each answer leads to a different problem.

Classify the constraints before accepting the remedy. Finite storage is a resource limit. Delivering every accepted record is a promise. A particular queue capacity is a choice. The belief that outages last at most two seconds is an assumption requiring evidence. A larger queue may satisfy the burst requirement while leaving sustained overload untouched.

Then ask which work can disappear while preserving the agreed result. If a display needs only the latest reading, retaining every intermediate reading may be unnecessary. If the recorder promises an audit history, deleting readings violates the result. Changing a requirement needs agreement from whoever depends on it; discovering an inconvenient cost does not authorize silently weakening the promise.

Use this question at a design boundary: **Which requirement would fail if this component, copy, allocation, or synchronization step disappeared?** Give a concrete input or event sequence as the answer. If you cannot, investigate whether the step is necessary before making it faster. If you can, retain the obligation and compare other ways to fulfill it.

## Estimate the floor before tuning the implementation

A cost model separates work forced by the result from work introduced by a design. Consider a program receiving an arbitrary byte stream and forwarding every byte unchanged. For `N` input bytes, `N` bytes must reach the destination under that contract. Staging the entire input in another file adds a storage requirement that forwarding alone does not impose. It may still be worthwhile for replay or recovery, but those benefits need their own requirements.

Write units beside every estimate: bytes retained, calls made, records per second, or seconds waiting. Label idealized bounds and measured values separately. A lower bound identifies work a design cannot avoid under its model; it does not promise an implementation can reach that bound. System-call costs, scheduling, failure handling, and interactions between stages can account for a gap.

This gives optimization a direction: remove work whose purpose has disappeared, simplify what remains, then measure the suspected limit. Reuse familiar designs when they meet the contract; revisit their assumptions when the workload or requirements change.

## A repeatable reasoning cycle

Use the following sequence for a design decision or an investigation. An **invariant** is a property that must hold at specified boundaries, such as before and after each queue operation. A **progress argument** explains what lets the next step happen: a consumer must run and remove an item before a producer waiting on a full queue can proceed. State both; correct stored values alone do not explain whether the program will finish.

1. **Define the result.** State observable inputs, outputs, success, failure, and resource limits. Replace adjectives such as fast, safe, and robust with questions you can evaluate. If a request prescribes a mechanism, ask which outcome requires it.
2. **State the starting facts.** Separate requirements, external contracts, assumptions, and observations. Investigate a contract when uncertainty about it could invalidate your design.
3. **Find the state that must survive.** Ask what information a future decision needs. Distinguish owned resources from borrowed access.
4. **Write an invariant and a progress argument.** Identify what must remain true at each boundary and what permits the next step to happen.
5. **Compare plausible designs.** Evaluate at least two against the same constraints. Estimate unavoidable work and identify work each design adds. An alternative can be correct yet have an unsuitable cost or contract.
6. **Construct a counterexample.** Break an assumption, choose an awkward boundary, or interrupt progress. Predict the result before executing anything.
7. **Test and revise.** Use proof for consequences of a model and experiments for claims about an implementation or environment. Preserve failures as tests.
8. **Transfer the idea.** Change one requirement and work out which conclusions still hold. This distinguishes an understood argument from a memorized implementation.

A partial write forces you to remember progress, but it does not prescribe a variable name or data structure. A delayed callback forces you to decide who retains the unaccepted event. Derive the necessary information first, then choose its representation.

## Worked derivation: a recorder with a slow consumer

Consider a recorder receiving fixed-size records from a producer and forwarding them to a consumer. Ignore threads initially. Treat production and consumption as events that may occur in either order.

The requested properties are:

- Every accepted record must eventually be delivered, in acceptance order, if the consumer continues making progress.
- Storage for pending records is finite: at most `B` records.
- Initially no records are pending.
- The consumer may temporarily be slower than the producer.

These statements still omit a decisive policy: can the producer wait, or can an attempted record be rejected? Before choosing a queue representation, examine whether the requested behavior is possible.

Let `A(t)` be the number of records accepted by time `t` and `D(t)` the number delivered. With no loss or duplication, the number pending is:

```text
Q(t) = A(t) - D(t)
0 <= Q(t) <= B
```

This is bookkeeping under the stated model, not a performance measurement. If the producer must accept every attempted record immediately while the consumer may stop indefinitely, eventually `A - D` can exceed any finite `B`. Those requirements cannot all be maintained. A better data structure cannot make the contradiction disappear.

To see the pressure numerically, use a hypothetical constant-rate approximation: attempts arrive at 1000 records per second, the consumer delivers 800 per second, and storage holds 2000 records. If every attempt is accepted, the backlog grows by approximately 200 records per second and fills the buffer in about ten seconds. Bursts, scheduling, and discrete events change the trajectory; these numbers are an illustrative model, not results from the book's programs.

Changing a requirement creates several possible designs:

| Policy when full | What it preserves | What the caller must accept |
| --- | --- | --- |
| Wait for space | Every completed acceptance can be delivered | Producer latency may grow without a finite bound |
| Reject the new record | Existing accepted records remain intact | Caller handles rejection, retry, or loss before acceptance |
| Evict an old record | Recent records can continue arriving | The original promise to every accepted record changes |
| Spill to another store | A larger temporary backlog may be retained | Additional storage, latency, and failure behavior enter the contract |

Spilling is not infinite storage. It changes the resource budget and its failure cases. Likewise, retrying rejected records somewhere else merely moves a queue unless the caller also has a bound and an overload policy.

Choose waiting for this version. Our operation `push` completes successfully only after inserting a record. A blocked attempt has not yet been accepted. Now derive a representation.

An array with `B` slots avoids allocating per record. A bounded pool of linked nodes can also satisfy the contract. The array has simple contiguous storage; the pool makes some pointer-based operations convenient but adds links and lifetime bookkeeping. Nothing in the requirement makes the array the only correct answer. We select it because this queue needs only insertion at one end and removal at the other.

Let `head` identify the next record to remove, and let `count` be the number pending. For positive capacity, maintain:

```text
0 <= head < B
0 <= count <= B
the logical queue occupies count slots starting at head, wrapping at B
```

The next insertion position is mathematically `(head + count) mod B`. For example, with `B = 4`, `head = 3`, and `count = 2`, the pending records occupy slots 3 and 0, and the next insertion uses slot 1. In C, the implementation must also justify the integer arithmetic; mathematical addition and machine addition are different contracts. A bounded capacity and checked arithmetic, or an equivalent branch that avoids overflow, can make the representation valid.

At empty, `count == 0`; at full, `count == B`. A design storing only equal head and tail positions cannot distinguish those two states without another rule. It could reserve a slot, store a count, or add a phase bit. The apparent implementation detail is a consequence of needing to distinguish two observable states.

Trace a capacity-two queue before adding threads. Start with `head = 0` and advance it only when removing an item:

| Event | `head` afterward | `count` afterward | Pending records, oldest first |
| --- | --- | --- | --- |
| Insert A into slot 0 | 0 | 1 | A |
| Insert B into slot 1 | 0 | 2 | A, B |
| Attempt to insert C; wait for space | 0 | 2 | A, B |
| Remove A; slot 0 becomes reusable | 1 | 1 | B |
| Complete C's insertion into slot 0 | 1 | 2 | B, C |

The blocked attempt changes no queue state. It becomes an accepted record only when its insertion completes. That distinction connects the public promise to the representation.

## Turn a trace into an invariant argument

The five-row trace explains one execution. To justify every permitted sequence of queue operations, we need an argument that survives the next operation, whatever that operation is. This is the practical use of an invariant.

Start by making the content claim explicit: the sequence delivered so far is a prefix of the sequence accepted so far. Reading the `count` occupied slots from `head`, wrapping at `B`, gives exactly the remaining suffix, in order. These sequences are part of our reasoning; the implementation does not have to retain the entire history. The numeric bounds alone would not establish this claim. A queue could keep `count` in range while overwriting a record or returning the wrong one.

Now check three obligations, still treating operations as indivisible events:

1. **Establish the invariant.** With `head = 0`, `count = 0`, and positive capacity, both bounds hold. The logical sequence is empty, matching the absence of accepted records.
2. **Preserve it on insertion.** Insert only when `count < B`. The slot immediately after the logical sequence is free; write the new record there, then increase `count`. All old records keep their order and the new record becomes the newest. The count remains at most `B`. A full-queue wait changes none of this state.
3. **Preserve it on removal.** Remove only when `count > 0`. Return the record at `head`, advance `head` by one with wraparound, and decrease `count`. The returned record was the oldest; the remaining sequence has its previous order. The count remains nonnegative.

Initialization establishes the claim, and every allowed transition preserves it. Repeating those transitions therefore preserves the claim for any finite operation sequence. That is an induction argument, expressed in the terms an implementation must satisfy. It assumes the arithmetic is valid and the slot operations do what we specified; concurrency adds further obligations.

**Predict before continuing:** a capacity-two queue contains A then B. A faulty removal returns B, leaves A pending, and decreases `count` from 2 to 1. Which part of the invariant detects the bug? Would checking only the bounds detect it?

:::reveal Review the strength of the invariant

The bounds still hold, but removal delivered the newest record before the oldest. The content-and-order claim fails. A useful invariant must connect the stored representation to the caller's promise; a valid array index alone says nothing about FIFO delivery.

The argument also does not show that a waiting producer ever runs again. That is a progress obligation, considered next.

:::

Now add multiple threads. A producer cannot expose the incremented count before the inserted record is ready for a consumer, and two producers cannot independently claim the same slot. The representation requires a coordinated state transition. A mutex is one way to enforce it; a more specialized atomic algorithm needs its own proof. Calling the queue lock-free does not supply that proof.

## Safety does not imply progress

Suppose every queue operation holds the right mutex, yet a producer waits while holding that mutex until a consumer removes an item. No consumer can acquire the mutex to create space. Bounds may remain intact while the system makes no progress.

Separate two questions:

- **Safety:** can anything prohibited happen, such as reading an uninitialized slot or delivering an item twice?
- **Progress:** under the stated environment assumptions, can a waiting operation eventually proceed or terminate?

Progress claims need assumptions. If the consumer stops forever and the policy is to wait for space, an insertion may wait forever by design. If the system includes shutdown, specify which waiters it wakes and what happens to already accepted work. A claim of eventual completion that omits these conditions is stronger than the design supports.

In a condition-variable implementation, the queue state answers whether progress is permitted. Notifications invite a waiter to recheck that state. This is why the Chapter 9 derivation begins with a predicate and lifetime argument rather than treating a notification as a stored work item.

## Design an experiment that could change your mind

Before running a program, write a prediction and an observation that would challenge it. “The test should pass” is too weak. “After closing the queue, both blocked producers return rejection and accepted items are drained” names observable events and a contract.

Use tiny cases to expose mechanisms. Capacity one forces empty/full transitions. A payload split after the third header byte challenges parsing. Leaving one pipe writer open challenges a reader's termination. A failed allocation challenges whether the old object remains owned.

An informative experiment often distinguishes two explanations. If a consumer is waiting, it might have no input, or a producer might still own an unused write descriptor. A log of bytes produced alone cannot distinguish those states. A descriptor-ownership trace and a controlled closure can.

Do not claim an experiment proved more than it exercised. A successful run can support the correctness of a path. It cannot prove the absence of undefined behavior on every path, or guarantee a different operating system's behavior. Conversely, a counterexample can be enough to disprove a universal claim.

Performance experiments need the same discipline. Specify a workload and a hypothesis, choose a measurement boundary, preserve the observable result, and identify confounders. A lower elapsed time does not by itself prove fewer cache misses. Chapter 12 develops that reasoning with reproducible measurement tasks.

## A short session you can repeat

For a 60-minute study session, use this adaptable allocation:

| Time | Work product |
| --- | --- |
| 0–10 minutes | Requirement, claim ledger, and a predicted failure case |
| 10–25 minutes | Two candidate designs and their invariants |
| 25–45 minutes | A small implementation or experiment that discriminates between them |
| 45–55 minutes | Comparison with the reference reasoning and a corrected explanation |
| 55–60 minutes | One changed-assumption question to attempt in a later session |

Adjust these times to the problem; some labs need several sessions. Preserve an incomplete attempt so you can see where the reasoning stopped. Looking up a contract is part of the work. Before opening the folded reference material, save your prediction and design so you can distinguish what you derived from what the reference taught you.

## Know when to revise and when to stop

Review a chapter's central design using these six questions. They are this book's application of the reasoning method, not an externally certified assessment.

| Review question | Evidence the explanation must supply |
| --- | --- |
| What result is actually needed? | Observable success, failure, and resource limits, before selecting a mechanism. |
| What supports the starting claims? | Requirements, documented contracts, assumptions, and observations kept distinct. |
| What cannot be removed under those claims? | Necessary information or work, an invariant, and the conditions for progress. Quantified costs need units and a stated model. |
| What could be removed or replaced? | A simpler candidate and the exact requirement it preserves or violates. |
| What could defeat this explanation? | A concrete counterexample or an experiment that distinguishes competing explanations, with a prediction made first. |
| What would make the decision change? | A changed requirement or assumption and the resulting revision to the argument. |

Mark each answer **supported**, **conditional**, or **missing**, and point to its evidence. A conditional conclusion must name its assumption and what fails without it. Missing support blocks the corresponding claim; strong answers elsewhere do not compensate for it. When the subject is object identity or lifetime, deriving necessary state is more useful than inventing a timing formula. When the subject is performance, a plausible story needs a cost model and measurements before it becomes a speed claim.

For example, a proposed explanation that says “mapping removes explicit reads, so it must be faster” fails this review. It has not accounted for page-fault work or stated a workload and measurement boundary. A defensible revision compares mapping and buffered reading on the same stable input, checks equal counts, predicts which work changes, and measures it. Until then, the performance advantage is a hypothesis. Chapter 8 supplies the contracts and alternatives; Chapter 12 supplies the experiment discipline.

Apply the same standard to a design suggested by an expert: identify which premises and evidence support it. Fix a discovered defect, then revisit the affected reasoning. A review passes when all six questions have adequate answers within the stated scope, including clearly bounded conditional claims.

Record unresolved uncertainty and move on when the stated scope is supported. A new workload, platform, or requirement may give you a concrete reason to reopen the design.

## Derive it yourself

A monitoring display must retain the latest 100 accepted readings. It may discard older readings, and producers must never wait for the display to read them. Assume the reading operation and insertion are serialized for this exercise. Define the behavior of the 101st insertion, derive the state you need, and compare two representations. Identify which requirement differs from the lossless queue above.

Then change exactly one assumption: a display reader may still be using a returned pointer when an insertion arrives. Does your previous ownership argument survive? Write a concrete sequence of operations that forces a choice about copying, pinning, or synchronization.

:::reveal Review after your attempt

The latest-100 requirement allows an old reading to be evicted when a new one is accepted. An array ring can overwrite the oldest slot and advance its starting position; a bounded list can remove its first node and append another. Both can preserve the logical sequence, with different storage and access costs. A count distinguishes the initial partly filled state from a full ring.

Returning a pointer into a replaceable slot adds a lifetime problem. The display may retain the pointer to the oldest reading, then the producer overwrites that slot. Copying a reading for the display can give it an independent lifetime. Pinning the slot requires a policy when every reusable slot is pinned, which may conflict with never waiting and finite storage. Synchronizing access can prevent overlap but changes the latency contract. The extra assumption invalidates a previously adequate design; that is the point of the transfer exercise.

:::
