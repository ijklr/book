# My systems reasoning workbook

This file is for your own writing. The book builder does not overwrite it. Copy the session template for each problem, retain your first attempt, and add a revision after an experiment or review.

Read the book's opening method before using the template. The objective is an inspectable argument supported by code and evidence. You may use documentation; record when a source, hint, or implementation changed your reasoning.

## A session template

### Problem and success

What observable result must the program produce? What counts as failure? Which resource or latency limits apply? What is deliberately outside the contract?

My statement:

### Starting claims

| Claim | Requirement, contract, assumption, observation, or choice? | Support or way to check | Consequence if false |
| --- | --- | --- | --- |
| Fill in one important claim | | | |

### Candidate designs

Describe two viable approaches. Compare them under the same requirements. Identify whether a rejected approach is incorrect, too costly, or merely less convenient for this workload.

Candidate A:

Candidate B:

Decision and reason:

### State, ownership, and transitions

What information must survive the next call, read, signal, or scheduling event? Who owns each resource? Which access is borrowed, and when does that permission end?

State diagram or table:

Invariant at the relevant boundary:

Why initialization establishes it:

Why each transition preserves it:

Why cleanup does not invalidate a remaining user:

### Safety and progress

What must never happen? Under which environment assumptions can an operation make progress or terminate? What if another participant stops permanently?

Safety argument:

Progress argument and its assumptions:

### Prediction and counterexample

Give the smallest input, event ordering, or resource failure that could defeat your design. Predict the observable result before running it.

Counterexample candidate:

Prediction:

Competing explanation and a distinguishing observation:

### Experiment and result

Record the exact command, input, platform, compiler settings, relevant traces, and output. Label illustrative calculations separately from measured values.

Command and setup:

Observation:

What the observation supports:

What it does not establish:

### Revision and transfer

Which statement changed? Preserve the original prediction above. State a changed assumption and derive which decisions must be reconsidered.

Revised argument:

One changed assumption:

New consequences:

Regression case to keep:

Assistance and sources consulted:

Remaining concrete uncertainty:

## Turn the session into a chapter

Write a short explanation with this order: the problem, the constraints, the first plausible design, its counterexample or cost, the revised design, the invariant, the experiment, and the limits. Include enough context that a reader can challenge your conclusion without needing to watch your session.

Before calling the chapter ready for review, check that every important conclusion has a stated basis, every experiment has an interpretation narrower than its evidence, and every ownership handoff has a lifetime boundary. Ask a reader to change one requirement. Use the question you find hardest to answer as the next experiment.

## Progress record

Use the labels Recognize, Implement, Defend, and Transfer from Appendix D. Record evidence rather than a confidence score or an estimate of hiring odds.

| Problem | Current label | Evidence | Next variation |
| --- | --- | --- | --- |
| Your first session | | | |
