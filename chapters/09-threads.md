# Chapter 9 — Deriving a shared-state protocol

Four workers each complete 25,000 units of work. We want an exact final total, and we want every completed increment to be represented in one common counter while the workers run. Storage must stay bounded. The owner of that counter must survive every worker that can access it. If starting a worker fails, the program must finish dealing with the workers already started and report failure.

These requirements come before threads or locks. They describe an accounting problem with concurrent contributors. They also expose a distinction: obtaining a final sum and maintaining a live shared total are different requirements. Keep both visible while evaluating designs.

## Separate a requirement from a mechanism

Our **application contract** requires exactly 100,000 increments on success. Our **model** is that workers may be delayed between any operations; source-code order in one worker does not schedule another. The **platform contract** supplies shared memory, thread creation, and synchronization operations. Our **design choice** is to use a mutex for the common counter. An observed total of 100,000 on your machine is one execution that agrees with the contract. It is not the reason the design is correct.

Several implementations deserve consideration:

| Design | Requirement it can satisfy | Coordination it needs |
| --- | --- | --- |
| One worker | Exact final and live totals. | No concurrent counter access. |
| Per-worker counters | Exact final total after combining results. | A completion boundary before reading each worker's result. |
| Shared atomic counter | Exact updates to this one common counter. | Atomic operations for competing reads and updates; valid storage throughout. |
| Shared counter under a mutex | Exact updates to a common state that can include several related fields. | One locking rule for the entire protected transition. |

Where C11 atomics are supported, `atomic_fetch_add()` on an atomic integer can express the single-counter update without an application mutex. Atomic operations are not necessarily implemented without locks, and several atomic fields do not automatically form one atomic transaction. This chapter uses a mutex because the same reasoning extends to balances, queues, and other relationships among fields. Neither choice guarantees a speed advantage. [C11 draft N1570, sections 7.17.5 and 7.17.7.5](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf)

Before optimizing that shared update, find the decision that needs it. Suppose “live total” came from a dashboard that refreshes once a second. That refresh interval alone does not tell us whether the displayed count may lag, must be exact at each refresh, or must account for an operation before its completion is acknowledged. Those are different contracts, even if their screenshots look alike.

If delayed progress reports are acceptable, workers can accumulate contributions privately and publish batches through a synchronized protocol. That reduces shared updates but creates unpublished work and a final partial batch to handle. If an operation must enter the common total before it is acknowledged as complete, batching must delay acknowledgement too. Specify the permitted delay before selecting a batch size. This chapter keeps the original per-increment contract.

Threads share a process's address space and descriptors, while having separate stacks and execution state. Separate stacks do not make their objects inaccessible to other threads: the sample passes workers a pointer to an object on `main`'s stack. Its storage must remain alive, and accesses to it must follow the synchronization protocol.

## Break the obvious implementation on paper

Suppose two workers execute `++count` without synchronization. Before reading further, write a trace for two increments starting at seven. Represent each increment as a read, an addition, and a write. State the expected mathematical result, then find an ordering of those steps that loses an update.

:::reveal Review the counterexample

Worker A reads seven; worker B reads seven; A stores eight; B stores eight. Two attempted contributions leave eight in this simplified machine model. This exposes the missing coordination, but it is not a complete prediction of an actual C program. These unsynchronized accesses to an ordinary shared variable form a data race: different threads access the same location, at least one writes, and no synchronization orders the accesses. A data race gives undefined behavior, so a sometimes-wrong count is not the only possible outcome.

:::

The repair must therefore establish a valid ordering, not merely make the illustrated schedule less likely. `volatile` does not make an increment atomic or synchronize threads. Adding a sleep changes timing without creating the necessary relationship. Running a million successful trials does not add an ordering edge either.

We need an indivisible state transition relative to the other workers. A **mutex** permits one thread at a time to own a lock; the operations performed while it owns that lock form a **critical section**. Locking also provides memory synchronization. While workers can modify the ordinary counter, every read or write of that counter must hold the same mutex. Protecting writers alone would leave an unlocked progress display with a data race. [POSIX memory synchronization](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/V1_chap04.html)

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

Compare your failure path with `started`, the number of workers successfully created. If creation fails, earlier workers continue, and the main thread joins exactly those workers. Only then does it destroy the mutex. The shared object on `main`'s stack remains alive through all joins, so the workers' pointers to it stay valid. Moving the object to the heap would not remove that lifetime obligation.

`pthread_create()` passes the pointer value; it does not copy the object the pointer designates. Initialize each argument before creating its worker, because the worker may run immediately. If workers need separate arguments, keep one stable object per worker. Passing every worker the address of a changing creation-loop variable would give them shared mutable state, not separate snapshots. [POSIX thread arguments](https://pubs.opengroup.org/onlinepubs/000095399/functions/pthread_create.html)

The pthread creation and join interfaces return an error number directly rather than using `-1` plus `errno`. The program therefore passes that returned value to `strerror`. It treats unexpected synchronization errors as fatal to this small process; a library would need an explicit propagation policy. Successful joining establishes that the selected worker has terminated. [POSIX thread creation](https://man7.org/linux/man-pages/man3/pthread_create.3p.html), [POSIX joining](https://man7.org/linux/man-pages/man3/pthread_join.3p.html)

## Correct state does not guarantee progress

The invariant proves a safety property: increments are not lost or duplicated by competing protected updates. Progress requires more. Workers must receive execution time, critical sections must finish, and the program must avoid cycles of waiting. These assumptions are not consequences of the final count.

Consider two workers and two locks. A holds the first and waits for the second; B holds the second and waits for the first. Neither corrupts the protected data. Neither can finish. Establishing one lock acquisition order removes this particular cycle. It does not promise a wall-clock completion bound under arbitrary scheduling.

Likewise, holding the counter lock while performing a blocking network read preserves exclusion but can prevent every other contributor from progressing. Short critical sections are valuable because they reduce the work that others must wait behind. First identify the complete state transition; then move independent work outside it without weakening the invariant.

The sample makes no speedup claim. A useful hypothesis would be: “For independent arithmetic work of this size, private results reduce coordination cost compared with a lock per update.” To test it, keep the work, compiler, worker count, and result checks comparable. Chapter 12 explains why a compiler may simplify a private counting loop dramatically. An observed improvement must be attributed to the generated work actually measured.

## Derive waiting from a queue predicate

Now change the problem: a consumer must wait until a queue contains an item or has been permanently closed. A predicate is a condition on shared state; here, `queue.count > 0 || queue.closed` lets the consumer proceed. Repeatedly checking an unchanged queue wastes execution time. A fixed sleep adds delay and still supplies no state synchronization. We want to suspend until progress may be possible.

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

### Trace a wakeup that finds no item

Let consumer A be waiting on an empty, open queue. Consumer B is processing an earlier item outside the queue lock. The producer adds one new item, `X`, and signals while holding the mutex. Choose a schedule in which A is awakened but B reaches the mutex first after the producer releases it:

| Step | Mutex owner | Protected queue state | What consumer A can do |
| --- | --- | --- | --- |
| Producer inserts `X`. | Producer | One item; open. | Remain asleep until notified. |
| Producer signals. | Producer | One item; open. | Become eligible to resume, but cannot yet own the mutex. |
| Producer unlocks; B acquires. | B | One item; open. | Still wait to reacquire the mutex. |
| B removes `X` and unlocks. | None | Empty; open. | Compete for the mutex. |
| A reacquires; its wait returns. | A | Empty; open. | Recheck the predicate and wait again. |

The notification did not reserve `X` for A. Replacing `while` with `if` would let A continue as though an item existed, potentially reading an empty slot or decrementing a zero count. This schedule needs no spurious wakeup; ordinary competition is enough to require the loop. [POSIX condition waiting](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_cond_clockwait.html)

Now inspect A's next wait. It checks the empty state while owning the mutex. A producer cannot change that protected state yet. When A calls `pthread_cond_wait()`, releasing the mutex and beginning the wait form one coordinated transition relative to a producer that acquires the mutex and then signals. There is no exposed interval equivalent to “unlock, then call a separate sleep.” An item present at A's locked check prevents it from waiting; a producer that inserts after the wait transition can wake it. The predicate and the coordinated wait cover both orders.

**Predict:** both consumers are now waiting on an empty queue. The owner closes the queue but uses `pthread_cond_signal()` once instead of broadcasting. The program no longer submits items. Is eventual termination of both consumers guaranteed? Does changing the waiting loop to `while (queue.count == 0)` help?

:::reveal Review the shutdown schedule

One signal need only wake one waiter. The other may remain asleep indefinitely because there will be no further state changes or notifications. Broadcasting makes every current waiter reconsider the closing state. Each then reacquires the mutex and sees `closed && count == 0`, allowing an ordinary return. A loop that tests only emptiness would send even awakened consumers back to sleep: closure must be part of the predicate as well as a reason to notify.

:::

The queue state remembers whether work exists. The notification merely tells sleepers to reconsider. A producer changes state under the same mutex and signals. Closing sets `closed` under the mutex and broadcasts so every waiting consumer can discover that no more work will arrive. Consumers still remove items from a closed queue until it is empty; only then are they finished. Without a closing state, an empty queue cannot distinguish “wait for a future item” from “there will never be another item.”

Use `signal` when one newly available item lets one waiting consumer proceed. Use `broadcast` for closure because every waiting consumer must recheck its exit condition. Neither operation transfers mutex ownership or stores a notification for future waiters. A later consumer examines the persistent queue state before deciding whether to wait. [POSIX condition notifications](https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_cond_broadcast.html)

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
