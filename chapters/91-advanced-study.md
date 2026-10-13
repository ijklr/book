# Appendix D — Build toward demanding engineering roles

The next step after the thirteen chapters is to build a system whose solution is not supplied. Choose a role family, attempt a relevant assessment in Appendix E, and use the result to select an advanced unit below. Each unit specifies a project and evidence of completion. [Appendix F](#appendix-f--from-c-systems-to-ai-infrastructure) supplies worked arguments for the AI infrastructure extensions.

## Calibrate the target

Use this book as a shared systems foundation for HRT, OpenAI, Anthropic, and other demanding engineering employers. Choose a **role before a brand**: backend services, inference performance, research engineering, and low-latency C++ work overlap, but their remaining gaps differ. C makes ownership and operating-system behavior visible; C alone is not complete preparation for every role.

The following public sources were checked on **2026-10-03**. They are a dated view of responsibilities and guidance, not a promise that a vacancy or interview format will remain unchanged.

**HRT:** its engineering guide discusses fundamentals, communication, coding, debugging, and design, with greater machine-level depth for C++ roles. Its role overview connects core development with C++, OS internals, CPU architecture, and networking, while algorithm development emphasizes statistical modeling. [Engineering and interviewing](https://www.hudsonrivertrading.com/hrtbeat/engineering-and-interviewing-at-hrt/), [role overview](https://www.hudsonrivertrading.com/work-at-hrt/).

**Production infrastructure:** OpenAI's ChatGPT Infrastructure role describes concurrency, consistency, asynchronous work, failure handling, observability, and controlled rollout and rollback. It asks for a strong general-purpose language and production systems experience; GPU programming and ML research are not listed as universal prerequisites for this work. [ChatGPT Infrastructure](https://openai.com/careers/software-engineer-chatgpt-infrastructure-london-uk/).

**Inference performance:** OpenAI's performance role connects first-principles reasoning and microbenchmarks to latency, capacity, and serving cost. Anthropic's inference-engine role connects systems languages such as Rust/C++ with batching, memory, accelerator execution, and measured performance bounds. These are reasons to add a specialized path after the systems foundation. [OpenAI performance optimization](https://openai.com/careers/software-engineer-inference-performance-optimization-san-francisco/), [Anthropic inference engine](https://job-boards.greenhouse.io/anthropic/jobs/5418323008).

**Research engineering and evaluation:** OpenAI describes large distributed ML systems and correct ML implementations. Anthropic's post-training evaluation role emphasizes Python, measurement quality, investigation of regressions, and production evaluation; statistics and experimental design are additional strengths. This path needs learning beyond a C systems course. [OpenAI Research Engineer](https://openai.com/careers/research-engineer-san-francisco/), [Anthropic model evaluations](https://job-boards.greenhouse.io/anthropic/jobs/5198255008).

**OpenAI interview preparation:** the published guide describes team-dependent assessments and values solution design, code quality, performance, tests, communication, and collaboration. It says allowed AI tools depend on the assessment. Use the instructions for your interview instead of assuming one policy or sequence applies everywhere. [OpenAI interview guide](https://openai.com/interview-guide/).

**Anthropic interview preparation:** its careers page encourages concrete evidence such as independent work and open-source contributions. It describes live technical coding with lookup allowed while expecting familiarity with syntax and standard libraries. Permission to consult documentation does not establish permission to use an AI assistant; follow the applicable interview instructions. [Anthropic careers](https://www.anthropic.com/careers).

The plan below is the book's interpretation of these responsibilities, not an employer syllabus. A posting that requests years of production ownership describes experience that completing a book cannot establish. Match level as well as subject. For another firm, read one current role, identify its three central responsibilities, and map each to a demonstration you can produce.

| Target role family | Foundation in this book | Next extension | Evidence to produce |
| --- | --- | --- | --- |
| Systems/backend infrastructure | Resource ownership, processes, threads, protocols, event loops | Distributed state and partial failure; persistence; monitoring; rollout and rollback; role-relevant language | A bounded service with failure injection, an explicit recovery contract, and an incident report |
| Inference performance | Memory layout, concurrency, I/O, profiling, measurement | Model execution and numerical correctness; batching; accelerator memory and communication; relevant runtime/language | A performance prediction checked against measurements, with output quality and workload held accountable |
| Research engineering/evals | Reliable execution, data handling, reproducibility, debugging | Python and the relevant ML framework; mathematics; experimental design; evaluation validity | A reproducible experiment with a baseline, data split, error analysis, and limitations |
| HRT/C++ low-latency systems | Ownership, OS behavior, networking, algorithms, latency measurement | Idiomatic C++, machine architecture, workload-specific optimization | An event-replay system whose correctness and latency claims survive changed requirements |

Choose one extension first. GPU programming and research mathematics belong where the role calls for them; they are not entrance requirements for every software engineering path.

## What the current book does and does not establish

The copier handles partial I/O, the process examples collect child status, and the matcher handles arbitrary chunking. Each chapter asks you to derive the state and challenge its assumptions before revealing code. The table below identifies the additional work needed to apply those ideas independently at a larger scale.

| Current material | What needs substantially more work |
| --- | --- |
| A derived and supplied resizable buffer | Independently testing allocation failure and defending aliasing and invalidation contracts |
| A derived streaming matcher | Choosing an algorithm without the derivation supplied and implementing a changed API |
| A mutex-protected counter | Queue shutdown, several producers and consumers, contention, publication, and a synchronization argument |
| A single-connection blocking server | Independent connection state, bounded output, deadlines, half-close behavior, fairness, and recovery |
| A finite nonblocking pipe | Managing many resources whose lifetimes and readiness change independently |
| Mapping a stable regular file | Explaining page faults, copy-on-write, address translation, working sets, and operating-system observations |
| Reasoned debugging and measurement exercises | Independent investigations with unfamiliar code, generated-code inspection, and controlled measurements |
| C throughout | An idiomatic language bridge chosen for the role: C++, Python, or Rust, with its own lifetime, library, and failure behavior |
| Local thread and socket state | Durable state, retries after uncertain outcomes, replication, consistency, and failure across processes or machines |
| A correctly functioning program | Deployment, actionable monitoring, compatibility, incident recovery, and rollback under ongoing traffic |
| Exact byte-level results | Numerical tolerances, data leakage, statistical uncertainty, and evaluation validity for ML-oriented roles |

Attempt the assessment before deciding which foundation chapters to skip. Following an explanation and constructing one independently exercise different skills.

## Use three forms of evidence

For each major skill, collect a **working artifact**, a **reasoned explanation**, and an **independent demonstration**.

A working artifact includes the implementation, build instructions, tests, and contract. The explanation accounts for ownership, invariants, failure, and costs. The independent demonstration changes a requirement while the reference implementation is closed. Together they show what you can build, justify, and adapt.

Use four progress labels:

| Label | Observable evidence |
| --- | --- |
| Recognize | You can follow the explanation and predict a supplied example |
| Implement | You can build the specified behavior with documentation and explain your choices |
| Defend | You can justify correctness, handle counterexamples, and repair failures |
| Transfer | You can adapt the idea to an unfamiliar requirement without reconstructing the book's code from memory |

These are study labels, not hiring predictions. A useful goal is repeated evidence at Defend and Transfer for the core skills of your target role. Record assistance honestly so you can distinguish what you learned from what you already could do independently.

## Unit 1 — Algorithms as engineering decisions

Work on arrays, hash tables, heaps, balanced search structures, graph traversal, and binary search. For each, connect a complexity claim to a workload. Identify the state invariant before implementing a mutation. Explain the difference between expected, amortized, and worst-case cost.

**Build:** a merge utility for several sorted streams, followed by an index that supports insertion, lookup, and deletion. Begin with a simple reference implementation. Then choose a heap or hash-table design where it actually improves the stated workload. Include duplicates, empty inputs, extreme integer values, and adversarial insertion/deletion sequences.

**Completion evidence:** tests agree with an independent oracle; you explain why each operation preserves the invariant; a complexity argument counts the work the implementation actually performs. For example, requesting a small number of distinct values does not guarantee reading a small number of duplicate input records.

**Transfer task:** introduce a memory limit or change stable ordering requirements. Describe the resulting tradeoff before editing code. Learning a family of algorithms should make this conversation easier, rather than leave you searching for a memorized problem title.

## Unit 2 — Choose a language bridge

Keep C as a way to make machine and operating-system interfaces explicit. Choose one additional language from the role you intend to pursue. For a C++ role, add sustained practice writing idiomatic C++ rather than treating it as C with classes. Start with scope-based resource ownership, value semantics, references and views, move operations, standard containers, and exception guarantees. Add templates when a concrete abstraction benefits from them.

**Build:** a move-only descriptor owner and a small application that composes it with ordinary containers. Specify the empty state, move result, self-move policy, explicit close behavior, and how errors are reported. Exercise moving through a container, replacing an owned descriptor, and leaving a scope through an exception. A destructor's inability to report an ordinary return-value error should influence the interface.

**Completion evidence:** ownership is neither duplicated nor lost; borrowed references do not outlive their objects; a change to container capacity does not silently invalidate a view still in use; failure guarantees are stated and checked. Separate obtaining storage from establishing object lifetime. The C++ working draft's [object lifetime section](https://eel.is/c++draft/basic.life) is useful for investigating the distinction; match the standard edition to your chosen language mode.

**Transfer task:** replace a copying interface with a borrowed view. Explain precisely which caller behaviors become invalid, and decide whether that tradeoff is worth the saved copy. A faster interface with an unexplainable lifetime contract is unfinished work.

**Python alternative:** build a harness around a completed C program. Pass arguments without shell interpolation, enforce timeouts, check exit status, bound captured output, and retain inputs and results for replay. Add parsing, tests, and a documented environment. Explain which resource owns each subprocess and how cancellation reaches it. Appendix F extends the experiment and result-accounting practice; a later ML path adds framework and numerical knowledge.

**Rust alternative:** when a target role uses Rust, port one ownership-focused component. Explain moves, borrowing, error results, and thread-sharing constraints in its terms. Avoid translating C pointer patterns mechanically. The completion standard remains a clear contract, meaningful failure tests, and an independent variation. Learning every language at once is not a prerequisite for progressing through the other units.

## Unit 3 — Follow an operation through the machine

Connect source code to generated instructions, caches, address translation, faults, and scheduling. Learn enough assembly for the architecture you use to inspect a small optimized function. Ask which work happens once, once per element, once per cache line, or once per page. Do not assume that two machines share a cache-line size or a memory-access cost.

**Build:** a set of experiments that performs equivalent logical work with contiguous storage and dependent pointer traversal. Vary the working set and traversal order. Keep data generation outside the measured operation, preserve an observable result, and inspect generated code to check whether the intended work remains.

**Completion evidence:** you can distinguish an observation from its proposed mechanism. A slower traversal might involve dependency chains, prefetch behavior, cache locality, or address translation; elapsed time alone does not identify the cause. Provide repeated measurements, machine/compiler details, and evidence that supports or limits your interpretation.

**Transfer task:** predict an intervention, run it, and explain a neutral or contrary result. The successful outcome is a better-supported explanation, not a requirement that every attempted optimization wins.

## Unit 4 — Operating systems under observation

Use the process and mapping examples to study address spaces, copy-on-write, page faults, system calls, scheduling, and descriptor inheritance. Connect each concept to an observation and identify the limits of the observation. Distinguish the language contract from the kernel implementation and the filesystem behavior.

**Build:** an experiment notebook that traces a small process tree and records what changes when a process allocates, touches, forks, and modifies memory. Use a dedicated Linux environment for Linux-specific tracing and profiling; keep portable experiments runnable on macOS. A Linux-specific command is not evidence about macOS internals.

**Completion evidence:** you can explain why allocating an address range and touching its pages are different events, describe how parent and child data evolve after `fork`, and account for every inherited descriptor in a pipeline. Record measured values without treating them as portable constants.

**Transfer task:** diagnose a process that seems to hang despite having little CPU usage. Construct competing hypotheses involving input, a retained pipe writer, a lock, and a child that has not terminated. Choose observations that distinguish those hypotheses.

Linux performance tools may have environment-specific access restrictions. Consult the kernel's [performance-event documentation](https://docs.kernel.org/admin-guide/perf-security.html) when a measurement is unavailable; report that limitation instead of inventing a counter value.

## Unit 5 — Concurrency with a correctness argument

First implement a bounded queue using mutexes and condition variables. Specify what close means, which items are accepted, whether accepted items drain, and who joins the workers. Test capacity one: it forces transitions that a generous queue can hide.

**Build:** a worker pipeline with multiple producers and consumers, an explicit close operation, and startup-failure cleanup. Make each accepted item observable exactly once within a completed in-process run. That is a defined local property; it is not a claim about crash recovery or distributed delivery.

**Completion evidence:** every shared field has a synchronization rule; wait predicates are explicit; close wakes every class of waiter; state and synchronization objects outlive their users. Give the argument before citing a clean race-detector run. The [POSIX condition-wait specification](https://man7.org/linux/man-pages/man3/pthread_cond_wait.3p.html) explains the wait/mutex relationship your reasoning relies on.

**Transfer task:** after the mutex version is correct and measured, investigate a single-producer/single-consumer ring. Write down publication and reuse ordering, justify each atomic operation, and identify where your proof relies on having exactly one producer and one consumer. The C++ working draft's [data-race and synchronization rules](https://eel.is/c++draft/intro.races) are a reference for that investigation. Lock-free algorithms are an advanced extension, not a substitute for the queue's contract.

## Unit 6 — Services with resource budgets

Extend the framed protocol into a service with several simultaneous clients. Every connection needs persistent parsing and output state, a memory budget, a lifetime owner, and a deadline policy. Define how peer closure interacts with a reply that has not yet been sent.

**Build:** a framed echo or event service with a bounded number of connections, limited pending output per connection, and explicit overload behavior. One slow reader must not stall independent clients or grow memory without a bound. A client that sends a few bytes periodically must not extend an absolute request deadline indefinitely.

**Completion evidence:** deterministic tests fragment headers and bodies, stop clients from reading, close during each protocol phase, and verify cleanup. You can explain the per-connection state machine, fairness policy, and total-memory bound. Document where disk access or computation can still block the loop.

**Transfer task:** change the protocol to allow multiple requests on a connection. Explain what request/reply ordering means and what prevents unbounded queued replies. Merely adding an outer loop to the blocking example does not answer either question.

**Production extension:** let a client lose its connection after submitting work but before receiving acknowledgement. The client cannot infer whether the work executed. Define a request identifier, duplicate policy, retention bound, and recovery behavior before adding retries. A local queue's single-consumption property does not imply exactly-once effects after a crash. Then design a compatible protocol change: specify which metrics detect trouble during rollout and what rollback can restore, including whether new stored data remains readable. These are further design and implementation tasks, not properties supplied by the chapter's socket example.

## Unit 7 — Performance claims that survive questions

Correctness and measurement belong together. Give the benchmark a workload, an arrival model, and an observable result. Distinguish throughput from latency. For latency, state where a sample starts and ends, how waiting is counted, and whether overload causes rejection, queueing, or omission from the measurements.

**Build:** a reproducible benchmark harness for one of your completed programs. Report sample counts and a distribution, including tail behavior when enough observations support it. A client that waits for each reply before sending another request is a particular workload; it does not reproduce an externally imposed arrival stream.

**Completion evidence:** another person can reproduce the setup and understand its limits. Change one factor at a time. Preserve the same answers and failure behavior after optimization. Do not present nanosecond precision simply because a clock API exposes nanosecond units.

**Transfer task:** review an attractive performance claim and identify what evidence is missing. Rewrite it as a narrower statement supported by the actual experiment.

## Unit 8 — Integrate and explain a complete system

Choose one larger project. Both are study vehicles with explicit limits; neither reproduces a firm's production system.

**Event replay:** write a small synthetic event format containing a sequence number, an instrument identifier, a price, and a quantity. Specify widths, byte order, valid ranges, and the policy for duplicates or missing sequence numbers before implementing the decoder.

Build first a sequential reference model and parser; then a state index and reproducible snapshots; then optional bounded concurrency; finally measured improvements. Keep the original reference model as an oracle. A reproducible synthetic data generator makes malformed and out-of-order cases testable without access to market data.

**Local bounded job service:** begin with stable synthetic inputs and a deterministic worker whose answers can be checked independently. Add admission limits, a bounded queue, request identifiers, explicit job states, deadlines, cancellation, and shutdown. Retain results under a stated memory bound. Specify what survives a restart; persistence is an additional feature to design and test, not an implication of accepting a request. A simulator can explore scheduling before real subprocess workers add lifecycle failures.

Use [Appendix F](#appendix-f--from-c-systems-to-ai-infrastructure) for worked reasoning about fixtures, scheduling, and evaluation records. Keep queue delay, service time, end-to-end latency, rejection, timeout, and incorrect-result counts distinct. Report the denominator behind every rate. The fixture measures infrastructure behavior; a claim about model quality requires a real model, appropriate data, and justified evaluation methods.

**Completion evidence:** the repository contains a protocol contract, ownership and state diagrams, independent correctness tests, failure tests, a benchmark report, and a short design review. Explain an abandoned approach and the evidence that caused you to abandon it. Be able to make a requirement change while preserving the relevant tests.

**Transfer task:** present the design in five minutes, then accept a change such as a lower memory budget or a slower consumer. Explain the consequences, ask clarifying questions, and implement a small part of the adjustment. Communication should make the engineering easier to inspect.

## Practice an unfamiliar problem and explain the result

Rotate four practice sessions. These are original study formats, not predictions of an employer's interview rounds. Choose a language you can use confidently and record the documentation, hints, and tools available during each attempt.

| Session | Suggested timebox | Deliverable |
| --- | --- | --- |
| Coding | 45–60 minutes | Clarify a changed requirement, state the invariant, implement a small solution, and test an adversarial case |
| Debugging | 45–60 minutes | Reproduce an unfamiliar failure, compare two hypotheses, collect discriminating evidence, and verify a repair |
| Design | 30–45 minutes | Estimate workload and resource bounds, explain state and failure handling, then adapt to one changed constraint |
| Incident explanation | 15–20 minutes | Present symptoms, impact, timeline, evidence, mitigation, root cause, and recurrence prevention without hiding uncertainty |

Use a peer or prepare a sealed variation in advance. Afterward, identify the first unjustified assumption and one concrete improvement. Repeat with changed data or requirements so improvement requires transfer. Explain a failed approach as carefully as a successful one. For an actual interview, the employer's current instructions govern timing, tools, and permitted assistance.

## Fit the plan to the evidence

Begin with Appendix E's shared core, E1–E6. Use its first task for one focused session; spread the longer labs over separate sessions. The suggested timeboxes make work comparable across attempts. They do not define a hiring threshold. E7–E9 are optional extensions: choose them when they exercise a relevant gap, after their prerequisites are sound.

Proceed in three passes: first repair foundational gaps identified by the core assessment; next complete one language bridge and one Unit 8 project; then add a role-specific extension and repeat independent practice. Appendix F supplies intermediate worked examples between the chapters and those larger projects. An inference or research direction adds ML study at this stage; a backend direction can instead deepen distributed systems and operations.

Use the failed contract or missing evidence to choose the next unit:

| Gap exposed by the assessment | Study next | Evidence to bring back |
| --- | --- | --- |
| Task 1: ordering, duplicates, or hidden work | Unit 1: algorithms | A corrected invariant and a bound that counts examined input |
| Task 2: ownership or incremental parsing | Chapters 2 and 10, then Unit 2 | A minimal failing input and a repair that preserves allocation-failure behavior |
| Task 3: queue closure or worker lifetime | Unit 5: concurrency | Traces of blocked producers and consumers completing shutdown |
| Task 4: stalled clients or unbounded output | Unit 6: services | A bounded service whose independent clients continue making progress |
| Task 5: an unexplained timing difference | Units 3 and 7: machine behavior and measurement | A controlled experiment that distinguishes proposed causes |
| Task 6: a changed callback or ownership API | Unit 2 and the relevant capstone reasoning | A caller that correctly resumes paused work or transfers ownership |
| Tasks 7–9: retries, scheduling, or evaluation | Appendix F, then Unit 8 | A deterministic trace with complete resource and outcome accounting |

Once the baseline is recorded, spend the largest share of study time on the weakest relevant fundamentals. Keep a smaller recurring block for independent coding and spoken explanation. If you already demonstrate a skill repeatedly, move forward rather than rereading familiar explanations for reassurance.

Do not equate the eight-week introductory schedule in Appendix B with a deadline for this advanced stage. Decide the pace after learning your starting point, available weekly hours, target role, and application timeline. A short preparation window requires prioritization; it does not make all eight units fit by assertion.

Use the foundation's notebook for each advanced unit. Write the explanation from your actual predictions, failures, and corrections. Ask a reviewer to change one requirement and work through the consequences with you; that variation tests whether the argument transfers beyond the project you prepared.
