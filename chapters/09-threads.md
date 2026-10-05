# Chapter 9 — Deriving a shared-state protocol

Four workers each complete 25,000 units of work. We want an exact final total, and we want every completed increment to be represented in one common counter while the workers run. Storage must stay bounded. The owner of that counter must survive every worker that can access it. If starting a worker fails, the program must finish dealing with the workers already started and report failure.

These requirements come before threads or locks. They describe an accounting problem with concurrent contributors. They also expose a distinction: obtaining a final sum and maintaining a live shared total are different requirements. Keep both visible while evaluating designs.

## Separate a requirement from a mechanism

Our **application contract** requires exactly 100,000 increments on success. Our **model** is that workers may be delayed between any operations; source-code order in one worker does not schedule another. The **platform contract** supplies shared memory, thread creation, and synchronization operations. Our **design choice** is to use a mutex for the common counter. An observed total of 100,000 on your machine is one execution that agrees with the contract. It is not the reason the design is correct.

Three plausible implementations deserve consideration. One execution path can do all the work, avoiding coordination. Separate worker counters can be combined after the workers finish, avoiding competing writes. A shared counter can be protected during each update, providing the live common state we requested. The final-only variant would often favor private counters; this chapter intentionally retains the live-total requirement to study a shared invariant. None of these architectures acquires a speed advantage merely from its name.

Before optimizing that shared update, find the decision that needs it. Suppose “live total” came from a dashboard that refreshes once a second. That refresh interval alone does not tell us whether the displayed count may lag, must be exact at each refresh, or must account for an operation before its completion is acknowledged. Those are different contracts, even if their screenshots look alike.

If the owner accepts delayed progress reports and an exact final result, workers can accumulate contributions privately and publish batches through a synchronized protocol. This removes some shared updates but introduces unpublished work and a rule for publishing the final partial batch. If acknowledgement requires the contribution to be committed first, batching cannot preserve immediate acknowledgement without further changes. Write the permitted delay before selecting a batch size. The first-principles step is to challenge why each shared update exists; deleting it is valid only after the resulting behavior satisfies the agreed requirement. This chapter keeps the original per-increment contract.

Threads share a process's globals, heap, and descriptors, while having separate stacks and execution state. That platform arrangement makes a shared pointer possible. It does not answer when the pointed-to object may be read or destroyed. Those are obligations of our design.

## Break the obvious implementation on paper

Suppose both workers execute `++count` without synchronization. Before reading further, write a trace for two increments starting at seven. Represent each increment as a read, an addition, and a write. State the expected mathematical result, then find an ordering of those steps that loses an update.

:::reveal Review the counterexample

Worker A reads seven; worker B reads seven; A stores eight; B stores eight. Two attempted contributions leave eight in this simplified machine model. This exposes the missing coordination, but it is not a complete prediction of an actual C program. Unsynchronized conflicting accesses, at least one a write, form a data race when the C rules' ordering and atomicity requirements are absent. A data race gives undefined behavior. The implementation does not promise only a sometimes-wrong count.

:::

The repair must therefore establish a valid ordering, not merely make the illustrated schedule less likely. `volatile` does not make an increment atomic or synchronize threads. Adding a sleep changes timing without creating the necessary relationship. Running a million successful trials does not add an ordering edge either.

We need an indivisible state transition relative to the other workers. A mutex is a supplied mechanism for exclusive access and memory synchronization. All accesses that participate in the shared-counter protocol must follow the same rule: hold this mutex while workers can modify the counter. A hardware intuition about cache coherence cannot replace the C and pthread contracts.

## Derive the state and the invariant

Before creating workers, we need a counter initialized to zero, an initialized mutex, and storage for thread identifiers. During creation, we also need the number of successful creations. A bare array of identifiers cannot tell us which entries are valid after a partial failure.

Our main invariant is: **at every release of the counter mutex, the counter equals the number of increments committed so far**. Initially both are zero. A worker acquires the mutex, adds one, and releases it. No other participating worker can observe or modify the protected state in the middle of that transition. Induction over completed critical sections gives the accounting argument.

Two lifetime invariants accompany it: the shared object remains alive while any started worker may use it, and the normal and handled creation-failure paths join every successfully created joinable worker exactly once before destroying shared synchronization state. Unexpected synchronization failures follow a separate fatal-process policy in this example. A correct arithmetic invariant cannot rescue a dangling pointer.

A queue makes the distinction sharper. Its head, tail, occupancy, and elements jointly describe one state. Locking four assignments separately can expose a combination that never represented a valid queue. Protect the relationship across the whole transition. A lock is associated with an invariant; its job is not merely to decorate suspicious source lines.

The common counter may be read without its mutex after every worker has been joined successfully. That exception follows from the lifecycle: no worker can still modify it, and joining supplies the required synchronization. It is not a prediction that the workers are probably finished.

## Commit a design before opening the code

Write pseudocode with a creation loop, a join loop, and a cleanup point. Now assume the third creation fails. Mark which identifiers are valid, which workers may still run, and whether the object on `main`'s stack may be destroyed. Also predict whether reading the total before the join loop would be valid without locking.

:::reveal Reference implementation

{{include:examples/threads.c}}

:::

Build from the book directory:

```sh
make build/threads
build/threads
```

The required successful output is:

```text
count=100000 expected=100000
```

The Makefile uses `-pthread` when compiling and linking this program. The option expresses thread support to the toolchain; do not assume that naming a library alone is equivalent on every target.

Compare your failure path with `started`. If creation fails, earlier workers continue, and the parent joins exactly those whose identifiers were produced successfully. Only then does it destroy the mutex. The pointer into `main`'s stack is valid because its owner remains active through all joins. Moving the object to the heap would not remove that lifetime obligation.

The pthread creation and join interfaces return an error number directly rather than using `-1` plus `errno`. The program therefore passes that returned value to `strerror`. It treats unexpected synchronization errors as fatal to this small process; a library would need an explicit propagation policy. Successful joining establishes that the selected worker has terminated. [POSIX thread creation](https://man7.org/linux/man-pages/man3/pthread_create.3p.html), [POSIX joining](https://man7.org/linux/man-pages/man3/pthread_join.3p.html)

## Correct state does not guarantee progress

The invariant proves a safety property: increments are not lost or duplicated by competing protected updates. Progress requires more. Workers must receive execution time, critical sections must finish, and the program must avoid cycles of waiting. These assumptions are not consequences of the final count.

Consider two workers and two locks. A holds the first and waits for the second; B holds the second and waits for the first. Neither corrupts the protected data. Neither can finish. Establishing one lock acquisition order removes this particular cycle. It does not promise a wall-clock completion bound under arbitrary scheduling.

Likewise, holding the counter lock while performing a blocking network read preserves exclusion but can prevent every other contributor from progressing. Short critical sections are valuable because they reduce the work that others must wait behind. First identify the complete state transition; then move independent work outside it without weakening the invariant.

The sample makes no speedup claim. A useful hypothesis would be: “For independent arithmetic work of this size, private results reduce coordination cost compared with a lock per update.” To test it, keep the work, compiler, worker count, and result checks comparable. Chapter 12 explains why a compiler may simplify a private counting loop dramatically. An observed improvement must be attributed to the generated work actually measured.

## Derive waiting from a queue predicate

Now change the problem: a consumer must wait until a queue contains an item or has been permanently closed. Busy checking wastes execution time while nothing changes. A fixed sleep adds delay and still supplies no state synchronization. We want to suspend until progress may be possible.

A naive plan checks `queue.count == 0`, unlocks, then sleeps awaiting a notification. Predict the failure if the producer inserts and notifies between that unlock and the start of the wait. Another naive plan keeps the mutex locked while sleeping. Predict why the producer cannot repair the empty queue in that version.

The required operation must connect releasing the mutex with beginning the wait, preventing that gap relative to the producer's protected update. POSIX condition waiting provides this operation and reacquires the mutex before a successful return. A wakeup does not guarantee the predicate is true: another consumer may have taken the item, and spurious wakeups are permitted. [Condition-wait contract](https://man7.org/linux/man-pages/man3/pthread_cond_wait.3p.html)

That contract motivates the loop below. It is structural pseudocode in C syntax; a complete implementation must check errors and initialize all objects.

```c
pthread_mutex_lock(&queue.mutex);
while (queue.count == 0 && !queue.closed)
    pthread_cond_wait(&queue.changed, &queue.mutex);
/* While holding the mutex: remove an item, or recognize closed-and-empty. */
pthread_mutex_unlock(&queue.mutex);
```

The queue state remembers whether work exists. The notification merely tells sleepers to reconsider. A producer changes state under the same mutex and signals. Closing sets `closed` under the mutex and broadcasts so every waiting consumer can discover that no more work will arrive. Without a closing state, an empty queue cannot distinguish “wait for a future item” from “there will never be another item.”

This is also why cooperative shutdown is attractive: workers leave through ordinary control flow and release owned resources. Thread cancellation introduces additional questions about the locks and objects owned at the interruption point.

## Transfer the argument

1. Change one assumption: only the final total matters. Design per-worker results and identify which reads and writes still require synchronization. Explain why the common mutex can disappear without making synchronization disappear altogether.
2. Keep shared state, but replace the counter with an account balance and a transaction count that must change together. Show a schedule that breaks their relationship if each field uses its own separately released lock.
3. Build a bounded queue with one producer and two consumers. Specify predicates for empty, full, and closed states. Before coding, show how shutdown wakes every class of waiter and why each submitted item is consumed exactly once.

:::reveal Review after your attempt

For exercise 1, give each result one worker writer and read it only after joining that worker. Initialize arguments before creation and keep them alive through joining. Creation and joining establish the lifecycle and visibility relationships; removing the common counter only removes the need to serialize those independent writes.

For exercise 2, a reader can see the new balance and the old transaction count between separately protected updates. One invariant spanning both fields needs a protocol that preserves their joint transition, such as one mutex held across both updates.

For exercise 3, consumers wait while empty and open; the producer waits while full and open. Closing must notify both sides. Removal occurs under the queue mutex, so two consumers cannot claim the same slot. Exactly-once consumption additionally depends on what “consumed” means if processing outside the queue fails: removing an item and successfully processing it are different contracts.

:::

## Write your version

Write a design review centered on the third-worker creation failure and the empty-queue shutdown. For each, separate the safety argument from the progress assumptions. Include a rejected design and its concrete counterexample. End by identifying a requirement whose removal would let you delete shared state. A smaller ownership problem is often a stronger improvement than a more elaborate lock.

For a service variation, let the caller cancel a job after a worker starts using its input. A cancellation request changes what the caller wants; it does not establish that the worker stopped accessing memory. Draw separate transitions for cancellation requested, worker finished, and storage released. Test cancellation immediately before and after result publication. This is the same lifetime argument as joining before destroying shared state, now applied to a request that may outlive its connection. The optional scheduler assessment in [Appendix E](#appendix-e--an-independent-systems-assessment) asks you to preserve the associated resource budget too.
