# Appendix F — From C systems to AI infrastructure

A model service owns buffers, accepts work, waits for resources, and handles interrupted communication. The C chapters supply the tools for reasoning about those obligations. Here the workload changes while the questions remain: what must be retained, what permits progress, and what does the evidence establish?

This appendix develops three designs that connect Chapters 9–13 to AI infrastructure. They are worked arguments and lab specifications; the repository does not supply their implementations. Begin with deterministic simulated workers on your CPU. No paid API, model download, or accelerator is required. Appendix E Tasks 7–9 ask you to implement related contracts independently.

## Derive admission from retained memory

Suppose a service receives jobs containing an immutable input, a model version, and a deadline. A worker eventually returns a bounded result. Before choosing a thread count, answer a more basic question: what permits the service to accept another job?

For this study design, acceptance means the service has validated the request and reserved the maximum memory covered by its accounting model. Acceptance commits resources; it does not promise a successful computation. Rejection transfers no ownership. An accepted job eventually reaches a terminal outcome—success, failure, or cancellation—if workers and the coordinator continue making progress. Reporting deadline failure does not establish that the worker has stopped.

Assume a 64 MiB accounting budget, including 16 MiB reserved for fixed state. Each accepted job reserves 6 MiB covering its bounded input, metadata, working storage, and retained output. Assume these bounds are enforced and no additional allocation escapes the ledger. Then:

```text
fixed reservation + sum of live job reservations <= budget
16 MiB + n * 6 MiB <= 64 MiB
n <= 8
```

This is a bound on the declared accounting model. It is not a measurement or guarantee of process resident memory; stacks, allocator behavior, and libraries must be accounted for before making that larger claim. A real worker whose workspace is unknown invalidates the premise. Reducing a thread count does not by itself establish a memory bound.

Reserve before acceptance, under the same synchronization that changes the queue. Checking available space and updating it separately allows two submitters to spend the same capacity. Queued jobs retain reservations too. Otherwise eight running jobs plus an unbounded waiting list defeats the calculation.

Release a reservation only when the resources it covers are no longer needed. A queued job can be removed before any worker owns its input, but a running worker may still access that input after cancellation is requested. Marking the job canceled and immediately reusing its storage introduces a lifetime bug. Track the reported outcome separately from acknowledgement that execution and buffer access have ended. Completed replies also retain capacity until delivered or disposed of according to policy.

**Try a counterexample:** all eight slots are occupied when the oldest worker exceeds its deadline. A ninth request arrives. Under which event can its reservation succeed?

:::reveal Review the capacity transition

Deadline expiry alone releases nothing that the worker still owns. The ninth request must remain unaccepted or be rejected until enough ownership actually ends. If cancellation never stops the worker, reserved capacity can remain unavailable. A design that requires recovery from that condition needs an enforceable termination or isolation mechanism and a cleanup contract. Renaming the state does not supply one.

For a concrete trace, suppose all eight jobs are executing. The service rejects new work when full and keeps no result cache after a job is fully disposed of:

| Event | Live job reservations | Total reserved | Admission decision |
| --- | --- | --- | --- |
| Eight jobs are running | 8 | 64 MiB | Full |
| One job expires; cancellation is requested | 8 | 64 MiB | Still full; its worker owns storage |
| A ninth job arrives | 8 | 64 MiB | Reject it |
| The expired worker stops and all of that job's resources are released | 7 | 58 MiB | One 6 MiB reservation is available |
| A new submission reserves that capacity | 8 | 64 MiB | Accept it |

If a reply or outcome record remains retained, its storage must stay in the ledger. The retry design below makes that additional lifetime explicit.

:::

## Derive batching from a cost model

Batching groups several jobs into one worker invocation. It can reduce repeated setup work, but gathering a batch consumes time. Whether it helps depends on both the worker cost and the arrival pattern.

Use this deliberately simulated worker duration:

```text
T(b) = 4 ms + 2 ms * b, for batch sizes 1 through 4
```

There is one worker, jobs have equal size, and all members receive their results at batch completion. Ignore transfer and scheduling overhead. These are invented simulation parameters, not CPU or GPU performance measurements. Four individual jobs cost 24 ms of worker service; one batch of four costs 12 ms. Under a continuously supplied backlog, the modeled service capacities are approximately 167 versus 333 jobs per second. Neither number predicts request latency.

Now submit four jobs at simulated times 0, 3, 6, and 9 ms. Compare immediate single-job service with collecting four jobs, allowing at most 9 ms from the oldest queued arrival. At a shared timestamp, enqueue arrivals before choosing the batch, so the arrival at 9 ms joins the batch dispatched then:

| Policy | Completion times, in arrival order | Latencies from arrival | Mean latency |
| --- | --- | --- | --- |
| Single jobs | 6, 12, 18, 24 ms | 6, 9, 12, 15 ms | 10.5 ms |
| One batch of four, starting at 9 ms | 21, 21, 21, 21 ms | 21, 18, 15, 12 ms | 16.5 ms |

The batch uses less worker time and finishes the whole set earlier, while increasing mean latency. Now require completion strictly before 15 ms has elapsed since each arrival: three jobs meet that deadline under single-job service, but only one does in the batch. Treating equality as success would change those counts to four and two. The deadline rule matters as much as the duration calculation.

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

If attempts may overlap, give every attempt a unique ID. Serialize the transition from unresolved to terminal so two completions cannot both win. A successful attempt can commit only while the logical job remains unresolved. Later successes cannot overwrite the accepted result; their buffers still need cleanup. A retry limit or job deadline may instead commit failure, making later successes ineligible. Specify whether a timeout ends only an attempt or the whole logical job before implementing retries.

The invariant is **at most one accepted result per logical job**, not exactly one execution. A deterministic worker makes repeated answers easy to compare, but determinism alone does not suppress repeated work. For a stochastic worker, the accepted answer may depend on which attempt wins. Pinning a model version and settings gives the request meaning without promising identical outputs.

### Work through an overlapping retry

Take one logical job J with matching, valid replies from attempts A and B. For this trace, an attempt timeout permits another dispatch but does not commit job failure or disqualify the earlier attempt. There is no job-deadline expiry. Completion here includes confirmation that the attempt has stopped using its execution buffers; receipt of a reply alone would need a separate ownership rule otherwise.

| Event | Job's committed outcome | Attempts still executing | Required decision |
| --- | --- | --- | --- |
| Dispatch A | None | A | Reserve A's execution resources |
| A exceeds its reply timeout | None | A | Keep A's reservation; timeout does not establish termination |
| Dispatch B | None | A, B | Reserve B separately before dispatch |
| B completes successfully | B's result | A | Commit once; release B's execution storage and retain the result |
| A completes successfully | B's result | None | Discard A's redundant result and release A's execution storage |
| A duplicate of B's reply arrives | B's result | None | Ignore the duplicate; do not release B's resources a second time |
| The client retries J | B's result | None | Return the stored outcome without another result-acceptance event |

The logical job and its attempts require separate state because their lifetimes diverge. After B wins, J has an answer while A still owns resources. After A stops, no attempts execute, yet the cached result and identity record still occupy storage. A single `done` flag cannot tell the coordinator which of these resources it may release.

**Predict a variation:** A's success arrives just before B's success. Then consider a different policy in which J's absolute deadline commits failure before either success arrives. Which reply may win in each case, and what cleanup is still required?

:::reveal Review competing terminal events

Under the first policy, A may win even though its attempt timeout has elapsed. The timeout authorized another attempt; it did not invalidate A. B's later success is redundant. Reversing arrival order can therefore change the accepted result without violating the contract.

Under the second policy, the committed deadline failure has already resolved J, so neither later success can replace it. Both attempts still require cleanup when their execution actually ends. Serializing the terminal transition chooses one outcome; it does not stop workers or release their buffers by itself.

:::

**Exercise:** attempt A finishes, the coordinator commits its answer, the reply is lost, and the client resubmits. Then repeat the trace with the coordinator crashing immediately after the commit. Which retained fact makes the histories different?

:::reveal Review the restart boundary

During one lifetime, the job record lets the coordinator return its existing outcome. After restart, an in-memory record is gone. Continuing the guarantee requires durable identity and outcome state, an atomic commit rule, and recovery that restores that state before accepting retries. Committing a result durably before publishing success closes one window; a crash before that commit can still require repeated computation. Durable deduplication does not promise exactly-once execution.

External side effects add another boundary. If a worker changes another service and crashes before recording that change, a local result log alone cannot decide whether repeating the effect is safe. The study worker computes a result without external effects. Extending that contract requires coordination with the destination. Retention and expiry also matter: deleting deduplication records limits the period for which retries can recover old outcomes.

:::

## Evaluation is an accounting system too

A fast runner can still produce a misleading experiment. Begin with a manifest describing every intended case: stable ID, input, expected output or scoring rule, provenance, and split membership. Record generator version and seed for synthetic fixtures; record permitted source and preprocessing for external data. Keep model version, configuration, code revision, environment, and the exact manifest with each run.

Start with a deterministic baseline whose answers are independently checkable, such as the capstone's byte-pattern predicate compared with a simple whole-input oracle on bounded fixtures. This establishes runner behavior before adding model uncertainty. A file of canned successful replies cannot test timeout accounting or retry behavior. Add injected failures and preserve an outcome for every intended case.

Suppose 100 cases are submitted. Ninety complete before the deadline: 80 satisfy the scoring rule and ten do not. The other ten time out. These outcomes answer three different questions:

| Metric | Calculation | What it describes |
| --- | --- | --- |
| Correctness among completed cases | 80 / 90, about 88.9% | Answer quality conditional on completion |
| Completion rate | 90 / 100 = 90% | How much requested work returned an answer in time |
| End-to-end success | 80 / 100 = 80% | Requested cases with a correct answer before the deadline |

Report the counts with the denominators. Removing missing results changes the question. Do not invent completion latencies for timed-out cases; report their count and observation deadline separately from the latency distribution among completed cases.

### Work through an apparently faster report

Use a simulated workload of four requests dispatched together to four independent workers. Their fixed service durations are 10, 20, 100, and 200 ms; every computed answer is correct. The baseline waits for all four. A candidate runner uses the same workers but reports timeout at 50 ms and rejects later results. No worker computation has become faster.

| Request | Worker finishes at | Baseline report | Candidate report |
| --- | --- | --- | --- |
| A | 10 ms | Correct at 10 ms | Correct at 10 ms |
| B | 20 ms | Correct at 20 ms | Correct at 20 ms |
| C | 100 ms | Correct at 100 ms | Timeout at 50 ms |
| D | 200 ms | Correct at 200 ms | Timeout at 50 ms |

The baseline's mean completed-request latency is `(10 + 20 + 100 + 200) / 4 = 82.5 ms`. The candidate's is `(10 + 20) / 2 = 15 ms`. That smaller average describes a different population: the candidate excludes both slow cases. Its end-to-end success has fallen from `4/4` to `2/4`. Among the two cases both runners complete, latency is unchanged.

A 50 ms timeout may still serve a valid responsiveness requirement. Report that policy change and its consequences rather than attributing the lower conditional average to faster computation. If the workers continue after timeout, the candidate also retains their execution reservations until they stop, as in the admission example.

**Predict before measuring:** how would a genuine worker speedup differ in a trace of these same four request IDs? Which timestamps and outcomes would you need to retain?

:::reveal Review the comparison population

Record submission, dispatch, execution end, reported outcome, and result-acceptance time where applicable. A faster execution would move a worker's end time earlier for the same work. Changing only the reporting deadline changes which outcomes qualify for the completed-request average. Keeping per-request records lets you distinguish the two, including cases where both changes occur together.

For example, halving all four service durations produces 5, 10, 50, and 100 ms. With the rule that completion at the 50 ms deadline is too late, only the first two still succeed. A real speedup can coexist with unchanged success counts under this deadline, so report execution evidence as well as the aggregate outcome.

:::

### Define what the comparison measures

Distinguish test harness correctness from model quality. Exact byte equality can judge the parser fixture. It may not judge a task with multiple valid answers. Define the scoring rule before comparing candidates and inspect disagreements. If human judgment is needed, preserve the rubric and adjudication procedure. A larger aggregate score cannot explain whether changed behavior came from better answers, easier cases, or dropped failures.

When a model or threshold is fitted, separate training, development, and final held-out evaluation data. Training changes parameters; development informs choices; final evaluation estimates behavior on examples withheld from those choices. Keep related examples in the same split when their shared origin would leak information—for example, fragments from one source document should not quietly appear in both training and evaluation. Repeatedly selecting changes from the final set turns it into development data, whatever the filename says.

**Exercise:** optimization lowers mean completed-job latency while doubling timeouts. Write two possible explanations and one trace that separates them. Then change the workload to longer inputs without changing the original report. Identify precisely which conclusions no longer have supporting evidence.

## Optional bridge: a tiny learned worker

For an ML-focused path, add Python and basic linear algebra after the deterministic service works. Learn array shapes, dtypes, indexing, exceptions, environments, and tests. Follow the official [PyTorch basics sequence](https://docs.pytorch.org/tutorials/beginner/basics/intro.html) for tensors, data loading, model construction, differentiation, and saved parameters. Keep the first model small enough to train and evaluate on the CPU.

For example, generate two-dimensional points and assign each to one of two classes using a documented linear rule. Use fixed, separate training, development, and evaluation manifests. A linear classifier maps inputs of shape `[batch, 2]` to two class scores per example, also `[batch, 2]`; the first 2 counts input features and the second counts output classes. Labels have shape `[batch]`. Derive those meanings before selecting a layer: matching dimensions or successful broadcasting do not establish the intended calculation. The known rule checks the generator, while a constant-class predictor provides a quality baseline.

Training computes predictions and a loss, obtains gradients, and updates parameters. Gradients accumulate unless reset: explain where `optimizer.zero_grad()`, `loss.backward()`, and `optimizer.step()` belong in your loop. Inference uses the chosen parameters to produce predictions without a training update. [PyTorch's optimization tutorial](https://docs.pytorch.org/tutorials/beginner/basics/optimization_tutorial.html)

Treat evaluation behavior and gradient recording as separate controls. `model.eval()` changes behavior in modules such as dropout; it does not itself disable gradient recording. An appropriate `torch.inference_mode()` context disables autograd work and imposes restrictions on tensors created inside it, but does not put the model into evaluation mode. Check both contracts before using them together. [Module evaluation mode](https://docs.pytorch.org/docs/stable/generated/torch.nn.Module.html#torch.nn.Module.eval), [Inference mode](https://docs.pytorch.org/docs/stable/generated/torch.autograd.grad_mode.inference_mode.html)

Record seeds and software versions, and check repeated runs in the stated environment. A seed does not guarantee identical results across releases, platforms, or CPU and GPU execution. Preserve that uncertainty in the report. [PyTorch reproducibility notes](https://docs.pytorch.org/docs/stable/notes/randomness.html)

Only after choosing a specialization should you add accelerator-specific work: tensor layouts and kernels, device-memory ownership, synchronization, numerical precision, profiling, or distributed training. These require further study and suitable hardware for credible measurements. The CPU labs establish scheduler, failure, and experiment reasoning; they do not establish accelerator performance expertise.

Finish with a design review connecting one admission invariant, one ambiguous failure history, and one measurement limitation. Change one requirement and revise the design. That transfer is the skill the bridge is intended to develop.
