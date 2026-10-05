# Appendix F — From C systems to AI infrastructure

A model service still owns buffers, accepts work, waits for resources, handles interrupted communication, and produces evidence about its behavior. The C chapters give you a way to reason about those obligations. The next step is to change the workload while keeping the discipline: define success, derive necessary state, challenge an assumption, and measure the remaining uncertainty.

This appendix develops three designs that connect Chapters 9–13 to AI infrastructure. They are worked arguments and lab specifications; the repository does not supply their implementations. Begin with deterministic simulated workers on your CPU. No paid API, model download, or accelerator is required. Appendix E Tasks 7–9 ask you to implement related contracts independently.

## Derive admission from retained memory

Suppose a service receives jobs containing an immutable input, a model version, and a deadline. A worker eventually returns a bounded result. Before choosing a thread count, answer a more basic question: what permits the service to accept another job?

For this study design, acceptance means the service has validated the request and reserved its maximum accounted memory. A successful submission is distinct from successful computation. Rejection transfers no ownership. Accepted jobs eventually receive a terminal outcome if workers and the coordinator continue making progress; a deadline supplies a failure policy, not evidence that computation has stopped.

Assume a 64 MiB accounting budget, including 16 MiB reserved for fixed state. Each accepted job reserves 6 MiB covering its bounded input, metadata, working storage, and retained output. Assume these bounds are enforced and no additional allocation escapes the ledger. Then:

```text
fixed reservation + sum of live job reservations <= budget
16 MiB + n * 6 MiB <= 64 MiB
n <= 8
```

This is a bound on the declared accounting model. It is not a measurement or guarantee of process resident memory; stacks, allocator behavior, and libraries must be accounted for before making that larger claim. A real worker whose workspace is unknown invalidates the premise. Reducing a thread count does not by itself establish a memory bound.

Reserve before acceptance, under the same synchronization that changes the queue. Checking available space and updating it separately allows two submitters to spend the same capacity. Queued jobs retain reservations too. Otherwise eight running jobs plus an unbounded waiting list defeats the calculation.

Keep the accounting rule attached to ownership: memory becomes available when no owner can still access it. A cancellation request can remove a queued job, but a running worker may still hold its input. Marking that job cancelled and immediately reusing its storage introduces a lifetime bug. Separate the reported outcome from acknowledgement that execution and buffer access have ended. Completed replies also retain capacity until delivered or disposed of according to policy.

**Try a counterexample:** all eight slots are occupied when the oldest worker exceeds its deadline. A ninth request arrives. Under which event can its reservation succeed?

:::reveal Review the capacity transition

Deadline expiry alone releases nothing that the worker still owns. The ninth request must remain unaccepted or be rejected until enough ownership actually ends. If cancellation never stops the worker, reserved capacity can remain unavailable. A design that requires recovery from that condition needs an enforceable termination or isolation mechanism and a cleanup contract. Renaming the state does not supply one.

:::

## Derive batching from a cost model

Batching groups several jobs into one worker invocation. It can reduce repeated setup work, but gathering a batch consumes time. Whether it helps depends on both the worker cost and the arrival pattern.

Use this deliberately simulated worker duration:

```text
T(b) = 4 ms + 2 ms * b, for batch sizes 1 through 4
```

There is one worker, jobs have equal size, and all members receive their results at batch completion. Ignore transfer and scheduling overhead. These are invented simulation parameters, not CPU or GPU performance measurements. Four individual jobs cost 24 ms of worker service; one batch of four costs 12 ms. Under a continuously supplied backlog, the modeled service capacities are approximately 167 versus 333 jobs per second. Neither number predicts request latency.

Now submit four jobs at simulated times 0, 3, 6, and 9 ms. Compare immediate single-job service with collecting four jobs, allowing at most 9 ms from the oldest queued arrival:

| Policy | Completion times, in arrival order | Latencies from arrival | Mean latency |
| --- | --- | --- | --- |
| Single jobs | 6, 12, 18, 24 ms | 6, 9, 12, 15 ms | 10.5 ms |
| One batch of four, starting at 9 ms | 21, 21, 21, 21 ms | 21, 18, 15, 12 ms | 16.5 ms |

The batch uses less worker time and finishes the whole set earlier, while increasing mean latency. A 15 ms deadline from each arrival would also change which jobs succeed. State how equality at the deadline is treated before testing it. Batching is a tradeoff you can derive; a larger batch is not automatically a better service.

Give the scheduler a maximum batch size, a maximum wait measured from the oldest queued job, and absolute job deadlines. A new arrival must not reset the oldest job's wait. Before dispatch, expire queued work according to the contract and reserve any batch-specific workspace. In this toy model, workspace is already covered by the per-job reservations; another implementation must justify that assumption separately.

Start with FIFO selection and record its consequences. A large or slow job can delay smaller jobs; selecting only cheap jobs can starve expensive ones. Change one policy at a time. Measure waiting, service, completed-job latency, rejections, cancellations, and deadline failures on the same arrival trace. A benchmark that submits the next request only after a reply measures a different workload from an externally scheduled arrival stream.

**Exercise:** change the arrivals to 0, 20, 40, and 60 ms. Predict both policies with the 9 ms batching wait before simulating them. Explain why the previous saturated-capacity calculation does not describe this trace.

:::reveal Review sparse arrivals

Immediate service completes each job after 6 ms. Each batching timer expires without collecting another job, so each job waits 9 ms and then takes 6 ms: 15 ms latency. There is no shared setup saving. The arrival rate, rather than the saturated worker capacity, limits throughput here.

:::

## A lost reply changes what the caller knows

The worker finishes a request, but its reply disappears when a connection closes. A client timeout cannot distinguish that history from one in which computation never began. Retrying may be necessary for progress and may repeat execution. Derive the identity you need before writing a retry loop.

Distinguish three objects:

| Object | Meaning |
| --- | --- |
| Logical job | One requested computation, identified across retries |
| Attempt | One dispatch of that computation, with its own attempt identifier |
| Accepted result | The single result the coordinator commits for the logical job |

Use a bounded, single-coordinator study design whose guarantee lasts for that coordinator's lifetime. The job record binds an ID to canonical input bytes, model version, and computation settings. The same ID with different contents is a conflict. Matching retries attach to the existing job or retrieve its stored terminal outcome. Bound record and result storage; when full, reject new logical jobs. This initial design retains records until the run ends, making the cost of deduplication explicit.

Combining this design with the scheduler changes its budget: the earlier 6 MiB reservation assumed one execution per job. Reserve each overlapping attempt's workspace separately and retain that charge until it stops. Cached results and deduplication records remain charged after a reply is delivered; delivery does not end their ownership.

If attempts may overlap, give every attempt a unique ID. Serialize the transition from unresolved to terminal. A successful attempt can commit only while the logical job remains unresolved. Later successes cannot overwrite it; their buffers still need cleanup. A retry limit or deadline may instead commit failure, after which a late success remains late. Decide that policy before an ambiguous history occurs.

The invariant is **at most one accepted result per logical job**, not exactly one execution. A deterministic worker makes repeated answers easy to compare, but determinism alone does not suppress repeated work. For a stochastic worker, the accepted answer may depend on which attempt wins. Pinning a model version and settings gives the request meaning without promising identical outputs.

**Exercise:** attempt A finishes, the coordinator commits its answer, the reply is lost, and the client resubmits. Then repeat the trace with the coordinator crashing immediately after the commit. Which retained fact makes the histories different?

:::reveal Review the restart boundary

During one lifetime, the job record lets the coordinator return its existing outcome. After restart, an in-memory record is gone. Continuing the guarantee requires durable identity and outcome state, an atomic commit rule, and recovery that restores that state before accepting retries. Committing a result durably before publishing success closes one window; a crash before that commit can still require repeated computation. Durable deduplication does not promise exactly-once execution.

External side effects add another boundary. If a worker changes another service and crashes before recording that change, a local result log alone cannot decide whether repeating the effect is safe. The study worker computes a result without external effects. Extending that contract requires coordination with the destination. Retention and expiry also matter: deleting deduplication records limits the period for which retries can recover old outcomes.

:::

## Evaluation is an accounting system too

A fast runner can still produce a misleading experiment. Begin with a manifest describing every intended case: stable ID, input, expected output or scoring rule, provenance, and split membership. Record generator version and seed for synthetic fixtures; record permitted source and preprocessing for external data. Keep model version, configuration, code revision, environment, and the exact manifest with each run.

Start with a deterministic baseline whose answers are independently checkable, such as the capstone's byte-pattern predicate compared with a simple whole-input oracle on bounded fixtures. This establishes runner behavior before adding model uncertainty. A file of canned successful replies cannot test timeout accounting or retry behavior. Add injected failures and preserve an outcome for every intended case.

Suppose 100 cases are submitted. Ninety complete, of which 80 satisfy the scoring rule; ten time out. Then `80/90` is approximately 88.9% correctness among completed cases. If the declared end-to-end success rule requires a correct answer before timeout, `80/100` is 80% success over submissions. Report both denominators and the ten timeouts. Removing missing results silently changes the question. Do not invent completion latencies for timed-out cases; report their count and observation deadline separately from the distribution conditional on completion.

Distinguish test harness correctness from model quality. Exact byte equality can judge the parser fixture. It may not judge a task with multiple valid answers. Define the scoring rule before comparing candidates and inspect disagreements. If human judgment is needed, preserve the rubric and adjudication procedure. A larger aggregate score cannot explain whether changed behavior came from better answers, easier cases, or dropped failures.

When a model or threshold is fitted, separate training, development, and final held-out evaluation data. Training changes parameters; development informs choices; final evaluation estimates behavior on examples withheld from those choices. Split related examples together when shared origins would leak information. Repeatedly selecting changes from the final set turns it into development data, whatever the filename says.

**Exercise:** optimization lowers mean completed-job latency while doubling timeouts. Write two possible explanations and one trace that separates them. Then change the workload to longer inputs without changing the original report. Identify precisely which conclusions no longer have supporting evidence.

## Optional bridge: a tiny learned worker

For an ML-focused path, add Python and basic linear algebra after the deterministic service works. Learn array shapes, dtypes, indexing, exceptions, environments, and tests. Follow the official [PyTorch basics sequence](https://docs.pytorch.org/tutorials/beginner/basics/intro.html) for tensors, data loading, model construction, differentiation, and saved parameters. Keep the first model small enough to train and evaluate on the CPU.

For example, generate two-dimensional points and label them by a documented linear rule. Use fixed, separate training, development, and evaluation manifests. A linear classifier can transform input shape `[batch, 2]` into two class scores per example, shape `[batch, 2]`. Labels have shape `[batch]`. Derive those dimensions before selecting a layer; a tensor that broadcasts successfully can still express the wrong calculation. The known label rule supplies an oracle for the generator, while a constant-class predictor supplies a simple quality baseline.

Training computes predictions and a loss, obtains gradients, and updates parameters. Gradients accumulate unless reset: explain where `optimizer.zero_grad()`, `loss.backward()`, and `optimizer.step()` belong in your loop. Inference uses the chosen parameters to produce predictions without a training update. [PyTorch's optimization tutorial](https://docs.pytorch.org/tutorials/beginner/basics/optimization_tutorial.html)

Treat evaluation behavior and gradient recording as separate controls. `model.eval()` changes behavior in modules such as dropout; it does not itself disable gradient recording. An appropriate `torch.inference_mode()` context disables autograd work and imposes restrictions on tensors created inside it, but does not put the model into evaluation mode. Check both contracts before using them together. [Module evaluation mode](https://docs.pytorch.org/docs/stable/generated/torch.nn.Module.html#torch.nn.Module.eval), [Inference mode](https://docs.pytorch.org/docs/stable/generated/torch.autograd.grad_mode.inference_mode.html)

Record seeds and software versions, and check repeated runs in the stated environment. A seed does not guarantee identical results across releases, platforms, or CPU and GPU execution. Preserve that uncertainty in the report. [PyTorch reproducibility notes](https://docs.pytorch.org/docs/stable/notes/randomness.html)

Only after choosing a specialization should you add accelerator-specific work: tensor layouts and kernels, device-memory ownership, synchronization, numerical precision, profiling, or distributed training. These require further study and suitable hardware for credible measurements. The CPU labs establish scheduler, failure, and experiment reasoning; they do not establish accelerator performance expertise.

Finish with a design review connecting one admission invariant, one ambiguous failure history, and one measurement limitation. Change one requirement and revise the design. That transfer is the skill the bridge is intended to develop.
