# At the Boundary: Systems Programming in C

A foundational book for a reader who knows basic C and wants to learn systems programming on macOS and Linux, with advanced preparation paths for systems, infrastructure, and research engineering at firms such as HRT, OpenAI, and Anthropic.

Start with [the browser edition](BOOK.html) or [the assembled Markdown manuscript](BOOK.md). The foundation contains 13 chapters, complete source listings, exercises with selected answers, and writing prompts. Six appendices include a study plan, a role-based advanced curriculum, nine independent assessment tasks, and worked connections to AI infrastructure.

The [opening reasoning method](chapters/00a-reasoning.md) introduces first-principles thinking, with a source for Elon Musk's explanation of the approach. Worked examples throughout the chapters teach you to question inherited requirements, derive necessary state and costs, remove unnecessary work, and test uncertain assumptions before optimizing a design.

Start career preparation with [Appendix D: the advanced study track](chapters/91-advanced-study.md), which separates shared fundamentals from role-specific C++, Python, ML, and accelerator work using official employer sources reviewed on October 3, 2026. Use [Appendix E: the assessment](chapters/92-diagnostic.md) to identify gaps. [Appendix F: from C systems to AI infrastructure](chapters/93-ai-systems.md) derives scheduling, retry, and evaluation decisions and provides optional labs. The advanced projects are specifications for your own implementations; the book does not supply a complete ML curriculum or predict hiring outcomes.

The examples cover memory ownership, file I/O, filesystem objects, processes, pipes, signals, memory mapping, threads, TCP framing, and nonblocking readiness. The final project is `sift`, a bounded-memory streaming search tool.

## Read and revise

- `BOOK.html`: self-contained reading edition with navigation and print styles; open in a browser and use Print to save a PDF if desired.
- `BOOK.md`: complete assembled manuscript, including all C listings.
- `chapters/`: editable chapter sources and appendices.
- `examples/`: 12 standalone teaching programs.
- `capstone/`: the three-file `sift` project.
- `tests/`: C matcher tests and Python behavioral tests.
- `VALIDATION.md`: evidence and platform limitations for this draft.
- [First-principles review](FIRST_PRINCIPLES_REVIEW.md): the review criteria, chapter evidence, and limits of the editorial assessment.

Edit chapter files and C sources, then run `make book` to rebuild the manuscript and browser edition. Source include directives insert the actual compiled C files into the book, avoiding a separate copy that can drift out of date.

## Build and run

Requirements: a C17 compiler, POSIX development interfaces, `make`, and Python 3. Python is used only for the tests and book builder. Run from this directory:

```sh
make
make test
make sanitize
```

The networking tests bind only to `127.0.0.1` on automatically assigned ports. They require local socket access; a sandbox that prohibits binding will prevent those tests from running. Each server accepts one connection and exits. The test runner supplies timeouts and cleans up its child processes.

```sh
printf 'ordinary\nneedle here\nneedle again\n' | ./build/sift needle
```

Expected output:

```text
(stdin):2
(stdin):3
```

`sift` returns 0 for matches, 1 for no matches, and 2 for an error. The book explains why every part of that interface matters.

Validation was performed on macOS with Apple Clang. The code targets shared macOS/Linux POSIX interfaces; Linux execution has not been verified in this workspace.
