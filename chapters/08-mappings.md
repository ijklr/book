# Chapter 8 — Reading a file through virtual memory

Suppose you must count occurrences of one byte in a binary file. The file may contain zero bytes and may be empty. You need an exact count, bounded index arithmetic, and a clear failure result. Begin with an ordinary, stable regular file whose size fits the process's address-space model. Those conditions are input assumptions to examine, not properties established merely by opening a path.

The counting problem does not require a memory mapping. A buffered read loop could solve it. We will compare that design with viewing the file through virtual memory, then choose a mapping so that its different ownership and failure boundaries become visible.

Before choosing, predict whether a successful setup call proves that every subsequent access will be fast and valid. What could another process change after the setup call returns? Those questions matter more than whether the scan is written with an array subscript or a function call.

## Derive the counting algorithm independently of I/O

Let `L` be the number of bytes to inspect, `wanted` the byte value, `i` the next position, and `count` the occurrences already seen. A useful loop invariant is:

```text
0 <= count <= i <= L
count equals the number of wanted bytes in positions [0, i)
```

Initially `i` and `count` are zero. If the next byte matches, increase `count`; then advance `i`. After processing exactly `L` bytes, the invariant provides the answer. The empty input already satisfies the completed condition and therefore has count zero.

This reasoning exposes two requirements that the storage interface must satisfy. Every accessed position must belong to the permitted range, and the bytes being inspected must have the stability required by “exact count.” A loop can stay within its numeric bounds while reading a mixture of versions. Bounds safety and snapshot consistency are different properties.

Can we remove most of the scan before making it faster? For arbitrary bytes, an exact answer, and no trusted summary, consider any algorithm that leaves one position unexamined. Construct two same-length inputs identical at every examined position: the unexamined byte matches `wanted` in one and does not in the other. The algorithm has the same evidence for both, but their correct counts differ. It therefore cannot guarantee the answer. In the worst case, the input contains information we must inspect throughout its length, even if one machine instruction examines several bytes.

This lower bound belongs to the problem's assumptions. A trusted count associated with the exact immutable version could eliminate a later scan; accepting an estimate could permit sampling. Both change the available evidence or the required answer. Merely choosing `mmap()` changes neither. Establish what work is necessary before arguing about how cheaply an interface performs it.

C strings would be the wrong representation for arbitrary binary input: an embedded zero is data, not an ending marker. An explicit length and `unsigned char` access preserve all byte values. The command accepts the desired value as decimal `0` through `255`; an argument itself cannot carry an embedded null byte.

## Compare ways to supply the bytes

A streaming design owns a fixed buffer and repeatedly calls `read()`. Its invariant extends the count across successfully received chunks. It can process a pipe as well as a regular file, and transfer failures appear at explicit call boundaries. Total input need not occupy one contiguous virtual range.

A mapped design asks the operating system to associate a file range with virtual addresses. The loop can access that range directly, and repeated random access may be convenient. Mapping does not establish that every page is resident or that later file changes are harmless. The operating system may perform work when the program first touches an address.

| Design | Useful consequence | Cost or obligation |
| --- | --- | --- |
| Buffered `read` | Bounded explicit buffer; works with streams; familiar partial-I/O handling. | Manage chunk boundaries and transfer progress. Concurrent mutation still needs a consistency policy. |
| Whole-file mapping | Direct indexing; a compact scan and convenient reuse of the same range. | Require a mappable object and enough contiguous virtual space; manage mapping lifetime and backing-file changes. |
| Positioned `pread` calls | Fetch selected file ranges without changing the descriptor's shared offset. | Requires an appropriate seekable object, offset arithmetic, and partial-transfer handling. |

`pread()` supplies the positioned-read contract; it does not supply a file snapshot or solve concurrent changes by itself. [Positioned I/O](https://man7.org/linux/man-pages/man2/pread.2.html)

Our one-pass byte counter has no requirement that makes mapping inevitable. The educational choice lets us explore another interface using an already understood algorithm. A performance decision would need measurements of a specified workload, including access pattern and cache state. Counting fewer explicit `read()` calls is not a measurement of all work performed.

## Specify the assumptions around a mapping

Virtual addresses identify locations in a process's address space. The operating system manages mappings at page granularity, but page size is not a universal constant such as 4096. If an algorithm needs it, query `sysconf(_SC_PAGESIZE)` and check the result. Our whole-file mapping starts at offset zero and lets the system select the virtual address, avoiding alignment arithmetic in this first version.

The roles of the statements in our design are different:

| Kind of statement | Statement used here |
| --- | --- |
| External contract | `mmap` creates an address-space mapping; zero length is invalid; its failure sentinel is `MAP_FAILED`. |
| Model assumption | The ordinary regular file remains stable throughout inspection and the selected range is usable. |
| Design choice | Use a read-only private mapping, reject excessive sizes, and special-case an empty file. |
| Observation | One run completed without a fault and had a measured duration on one machine. |

A positive observation does not validate the stability assumption for another producer. POSIX explicitly leaves effects of accesses into portions added or removed after mapping unspecified. The guarantee we need must therefore come from the application's file-lifecycle policy. [POSIX memory mappings](https://man7.org/linux/man-pages/man3/mmap.3p.html)

## Derive ownership from two independent lifetimes

Opening a file gives us a descriptor. Mapping it creates another kind of resource whose lifetime is separate:

```text
file descriptor: open ----------- close
mapping:                 mmap ---------------------- munmap
borrowed byte pointer:        usable during the mapping's valid lifetime
numeric count:                    accumulated -------- usable afterward
```

After successful mapping, closing the descriptor does not end the mapping. Conversely, closing the descriptor alone is not how we release the address range. `munmap()` ends that lifetime, after which pointers into it are unusable. [Mapping and descriptor lifetime](https://man7.org/linux/man-pages/man2/mmap.2.html)

The implementation therefore needs to retain the mapping's base address and length until release. Its scan pointer borrows access; it owns no independent lifetime. The final numeric count does not borrow the mapped bytes, so it can be printed after unmapping.

Derive the failure cleanup from acquired resources rather than from a generic label called “error.” Before mapping, only the descriptor is owned. After mapping and before closing, both are owned. During scanning, the mapping remains. A failed descriptor close after successful mapping still leaves a mapping to release. Drawing those states makes missed cleanup visible before the code grows more branches.

## Validate the boundary between integer domains

`fstat()` reports size in signed `off_t`; the mapping interface accepts a `size_t` length. Casting does not prove representability. Reject a negative size and check the positive value against the destination range before converting. Otherwise, the numeric value used for bounds could cease to mean the measured file length.

Our example also restricts length to `PTRDIFF_MAX`. This is an explicit whole-file design limit that keeps the region within the range representable by pointer differences. It is not a promise that every smaller mapping will succeed. Available virtual address space and other resource limits still matter.

For length zero, return the mathematical answer after closing the descriptor, without requesting a zero-length mapping. This adapts the operating-system contract to the application's broader input domain. Rejecting an empty file simply because one chosen mechanism cannot map it would confuse a mechanism restriction with the counting problem.

Before revealing the source, write the scan invariant and the three resource states. Predict the cleanup needed if mapping fails, if closing fails afterward, and if final output fails. Then choose whether a zero byte ends the scan and justify your answer from the input contract.

:::reveal Reference implementation

{{include:examples/mapcount.c}}

:::

The map is `PROT_READ` and `MAP_PRIVATE`. The failure test uses `MAP_FAILED`, not `NULL`. After a successful map, the descriptor is closed before the scan, making the independent lifetimes observable in the code. The scan advances only while `i < length`, and its count cannot exceed the validated length.

The program checks release and final flushing before reporting success. A computed answer that never reaches its destination is not a successful command result. Failed closes are not blindly retried; descriptor reuse and the interrupted-close issues in Chapter 3 require an explicit policy.

## Challenge the assumption that private means frozen

“Private mapping” sounds like a private snapshot, but that intuition supplies a guarantee the interface does not make. The flag describes how writes through a private mapping relate to the underlying object; it does not promise a frozen version immune to external mutation. A shared mapping has different sharing behavior, but it likewise does not add an application transaction or locking protocol. [Mapping flags](https://man7.org/linux/man-pages/man2/mmap.2.html)

A counterexample to “we checked the size, so reads are safe” is:

```text
reader observes positive file size
reader successfully maps that range
writer truncates the same file to zero
reader touches an address whose backing has disappeared
```

On the target systems, such access can produce `SIGBUS` rather than return an ordinary I/O error to the loop. Checking the size again only moves the interval in which truncation can occur. It does not prevent the writer's action after the second check. [Mapped-file access beyond the object](https://man7.org/linux/man-pages/man2/mmap.2.html)

The missing guarantee can be supplied in several ways. A producer can publish completed immutable file objects and leave existing objects unchanged. Cooperating readers and writers can use a locking protocol around the relevant lifetime. A reader can work on an independently produced stable copy, provided the copy operation itself has a consistency rule. These choices change producer behavior or establish coordination; switching to a different load syntax cannot create that agreement.

Ordinary reads may make transfer failures easier to handle, but a sequence of successful reads can still combine versions of a concurrently modified file. Avoiding a mapping fault and obtaining a coherent snapshot are separate requirements. Also separate sharing, synchronization, and durability: writable `MAP_SHARED` access does not by itself ensure that an application update is atomic or survives a crash. That would require a storage protocol beyond this counter.

## Test the algorithm and its adapter separately

Create disposable inputs in a temporary directory and predict each count:

```sh
make build/mapcount
map_demo_dir=$(mktemp -d)
printf 'a\000ba\n' > "$map_demo_dir/bytes"
: > "$map_demo_dir/empty"
build/mapcount "$map_demo_dir/bytes" 97
build/mapcount "$map_demo_dir/bytes" 0
build/mapcount "$map_demo_dir/empty" 97
```

Expect `2`, `1`, and `0`. Value `97` denotes lowercase `a` in the ASCII-compatible encoding of this test input. The second case demonstrates that a null byte does not end the scan; the third checks the adapter's empty-file path.

Try `256`, `-1`, and `12x` as byte arguments. These are usage failures with status `2`. An unavailable file, unsupported file type, or mapping failure produces status `1`; success prints one decimal count and returns `0`. Some virtual filesystems present objects whose reported sizes do not describe generated readable content. The ordinary-regular-file assumption excludes treating this interface as a universal stream reader.

The scan has a finite number of iterations under its validated length and stability assumptions. That is not a bound on wall-clock latency. Address access can trigger page-fault work, and output can block. A latency-sensitive design must identify and measure those dependencies; “the loop has no explicit I/O call” is not a progress or timing proof.

## Exercises

1. **Strengthen the invariant.** Extend the counter to all 256 byte values. State an invariant relating the sum of counters to the processed prefix, and explain the numeric bounds. Compare newline-byte count with the number of human-described text lines.
2. **Change the address-space constraint.** Design a mapping window for a requested offset and length. Derive alignment, displacement, overflow checks, and the address used for release. Compare this with retaining a fixed `read` buffer.
3. **Transfer: allow a writer.** The input can now be truncated while scanning, but the caller still demands an exact count of one complete version. Choose a producer/reader protocol and state why repeated metadata checks alone cannot establish its guarantee.

**Write your version.** Start with the counting invariant, then explain exactly which storage guarantees make it meaningful. Include one reason mapping is useful and one requirement for which another interface is a better fit.

## Selected answers

:::reveal Review after your attempt

1. After processing `i` bytes, each counter is nonnegative, each is at most `i`, and their mathematical sum is `i`. Each individual `size_t` counter fits because `i` cannot exceed the validated length. A file containing `a\nb` has one newline byte even though a reader may describe two lines; the chosen counting unit defines the answer.
2. For positive page size `p`, use `base = offset - offset % p` and `delta = offset - base`; the mapped length must cover `delta + requested_length`. Check file bounds, conversions, and addition overflow before mapping. Release the original mapping base and full mapped length, not the displaced data pointer. Windows preserve indexed access but add boundary state; a read buffer is simpler when the algorithm only needs a sequential pass.
3. Immutable published versions can let a reader finish an already opened version while a producer publishes another. A cooperative lock can instead prevent mutation for the scan's duration, at the cost of delaying a writer. Both supply a lifetime guarantee. Repeated checks leave an interval before the next access and cannot forbid mutation. A streaming rewrite changes the failure interface but still needs a version-consistency policy.

:::
