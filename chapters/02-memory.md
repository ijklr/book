# Chapter 2 — Memory, bytes, and ownership

Design a command that accepts arbitrary bytes from standard input and reproduces them exactly on standard output, but emits no input bytes until collection has completed successfully. If reading or storage allocation fails, report failure and discard the collected data. Input length is not known in advance. This version may use growing memory, but no temporary file.

Those requirements explain why this chapter buffers an entire input. Streaming each chunk immediately would save memory, but if the third read failed, the first two chunks would already have escaped. Here successful collection means reaching end-of-file without a read or allocation failure. Chapter 3 will relax the output-timing requirement and derive a different design.

## Find the cost forced by the requirement

For arbitrary binary input, compression cannot guarantee a small representation of every possible input. If two different accepted inputs lead to exactly the same retained state, the program cannot later reconstruct both correctly from that state alone. Thus the amount of information retained must grow with the input in the worst case. Moving the information into a temporary file changes where the cost is paid; it does not eliminate it.

An unlimited-input promise also cannot guarantee success on a finite machine. Our contract must permit failure when storage or representable sizes are exhausted. This is a requirement correction, not an implementation defect. A program that silently truncates the input would be changing the requested result instead.

Within those constraints, compare three representations. A fixed array is simple and predictable but introduces a maximum accepted length. A linked list of chunks avoids moving earlier chunks, at the cost of per-chunk metadata and a less convenient contiguous view. A growable contiguous array gives us simple indexing and one allocation to own, but growth can relocate it. We choose the third representation to study that tradeoff; it is not the only correct design.

## Separate language facts from allocation policy

| Claim | Its status in this design |
| --- | --- |
| An object can be accessed only within its valid lifetime and permitted bounds. | An external C contract. |
| A failed positive-size `realloc` leaves the original allocation available. | An external allocation contract. |
| Capacity doubles when more space is needed. | Our growth policy, not an allocator guarantee. |
| A particular run grew an allocation without changing its printed address. | An observation that cannot justify retaining old pointers after successful reallocation. |
| Input buffers passed to append do not overlap our allocation. | An API precondition selected to simplify ownership. |

C describes objects, types, storage duration, and observable behavior. “Stack” and “heap” describe usual implementations, not mandatory physical regions. A local array generally has automatic storage duration; a file-scope object or local `static` has static storage duration. Allocated storage remains usable according to the allocation interface's lifetime rules. Copying a pointer does not extend the pointed-to object's lifetime.

We use ownership as a design agreement: one component is responsible for releasing an allocation. Other components borrow access for a specified interval. Copying a pointer creates another way to reach the same storage; it does not create another allocation or another independent right to free it. C does not enforce ownership, so control flow and interface contracts must make the agreement reviewable.

## Derive what must be remembered

To reproduce the input, we need an allocation reference and the number of stored bytes. To reuse spare allocation space without repeatedly asking for more, we also retain capacity. Capacity is optimization state; correctness could instead use an exact-size allocation after each append.

Let `len` count stored bytes and `cap` count allocated byte slots. Both use `size_t`, the unsigned type used for object sizes. The invariant is `0 <= len <= cap`. Positive capacity means a live allocation of at least that size; our initial and destroyed states use zero capacity and a null pointer. The first `len` bytes contain exactly the accumulated input. Spare slots are not input, even if they happen to contain convenient values.

An append operation must preserve the existing prefix, copy the new suffix, and publish the new length only after that suffix exists. Reserving space may change the allocation but must not change the logical input. Destruction releases the allocation and returns the object to its empty state. These transitions explain the functions we need before we decide their names.

Why not recover length with `strlen`? An input consisting of bytes `41 00 42` contains three bytes, but `strlen` would report one: the zero byte ends a C string. Using that result as the output length would discard both the zero and the final byte. A byte sequence therefore needs its own length. It need not have a terminator at `data[len]`, which might be outside the allocation.

`sizeof` measures C bytes; `CHAR_BIT` reports their bit width. Our macOS and Linux targets have eight-bit bytes. `sizeof pointer` still measures the pointer object, not an allocation reached through it. Character types can inspect object representations, but raw structures do not become portable file formats: padding, widths, and byte order remain separate encoding decisions.

## Reject two convenient but invalid transitions

Suppose a growth attempt assigns its result directly to the only owning pointer. If allocation fails, that pointer becomes null while the original allocation still exists. The program has lost the reference needed to read or release it. A temporary result allows us to commit the new owner only after success.

Now suppose the program computes a required size by adding first and checking afterward. Use an imaginary unsigned size type whose maximum is 255. With `len = 250` and an append of 10 bytes, the sum wraps to 4. A request for four slots cannot make a ten-byte append at offset 250 safe. The inequality must be rearranged before evaluating the sum: reject when `n > SIZE_MAX - len`. For arrays of larger elements, check the element count against `SIZE_MAX / sizeof *data` before multiplying.

Unsigned wrapping is defined behavior, but it can still violate our contract. Signed overflow is undefined behavior. These allocation and arithmetic rules are documented in sections 6.2.5 and 7.22.3 of the [WG14 C11 committee draft](https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf), background for the C17 subset used here. Language-defined does not mean application-correct.

## Account for work as well as safety

An exact-size growth request on every one-byte append may repeatedly move all previous bytes. Under a model where each growth relocates the buffer, copying costs sum as `1 + 2 + ... + (N - 1)`, which is quadratic. An allocator may sometimes extend in place, but our design cannot depend on that observation.

Doubling capacity changes that sum. If capacities grow through 64, 128, 256, and 512 bytes, moving the earlier contents copies at most `64 + 128 + 256 = 448` bytes. More generally, the earlier capacities form a geometric series bounded by the final capacity. While doubling continues, relocation therefore adds linear work in the accumulated input size under this model. The tradeoff is unused capacity.

The reference starts at 64 and checks for overflow before each doubling. If doubling would overflow, it requests the exact required size instead. That fallback preserves arithmetic safety but can cause repeated exact-size growth near `SIZE_MAX`; the geometric-series bound does not cover that regime. This argument describes the growth policy, without claiming a particular allocator's timing.

Termination has assumptions too. Each growth-loop iteration increases capacity or selects the final required size. Each input iteration consumes bytes, encounters an end, or reports a read failure. An endless source need not finish; finite available memory may be exhausted first. We have proved local progress, not a deadline or guaranteed success for every source.

## Derive it yourself

Before opening the code, draw a buffer with `len = cap = 64` and one borrowed pointer into its allocation. Trace a one-byte append for both allocation success and failure. Which fields may change in each case? Which pointer must the caller stop using after successful growth?

Next, predict what happens if an append source itself points into the allocation being grown. Would replacing `memcpy` with `memmove` repair every possible failure? Finally, compare withholding output until input collection succeeds with promising that output is all-or-nothing. Are they equivalent when the eventual output operation can fail halfway through?

:::reveal Reference implementation

{{include:examples/buffer.c}}

:::

The reference's `replacement` variable and delayed `len` update implement separate commitments: first secure capacity, then establish the new logical contents. An unsuccessful append preserves the original buffer, given its documented preconditions. Cleanup can therefore release exactly one owned allocation or none, whichever input iteration failed. The buffer object itself has automatic storage duration; its `data` member owns a separate allocation. Returning from `main` ends the first lifetime, while `buffer_destroy` explicitly ends the second.

The input loop uses `fread(chunk, 1, sizeof chunk, stdin)`. Its return value counts elements; because each element is one byte, it also counts bytes. A short result means EOF or a read error, so the loop checks `ferror` before accepting collection as complete. It may have obtained a final prefix before detecting an error; the command still discards the entire collection. This differs from the descriptor-level `read` in Chapter 3, where a short successful result need not mean EOF. See the [fread manual](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man3/fread.3.html).

Successful collection only permits output to begin. A later `fwrite` or flush can fail after exposing some bytes. The command returns 0 after successful collection and output, 1 for a detected data-path failure, and 2 for invalid arguments. Like Chapter 1, it retains the default signal policy, so termination by a signal is a separate outcome.

## Follow one append across the ownership boundary

Take a buffer with 60 stored bytes and capacity 64. A separate input array contains six new bytes. The required length is 66, so the four spare slots are insufficient. The growth policy chooses capacity 128. Here `[a, b)` means offsets from `a` through `b - 1`:

| Point in a successful append | `len` | `cap` | What is established |
| --- | --- | --- | --- |
| Entry | 60 | 64 | `[0, 60)` holds the existing input |
| Required-size check | 60 | 64 | Adding six is representable; no buffer state has changed |
| Successful reservation | 60 | 128 | The current allocation holds the preserved prefix and sufficient space |
| Copy six bytes to offset 60 | 60 | 128 | `[60, 66)` now contains the new suffix |
| Commit the length | 66 | 128 | `[0, 66)` is the accumulated input |

Capacity and length change at different moments because they express different facts. Reserving 128 slots establishes room, not 128 bytes of input. Advancing `len` to 66 before the copy would claim that the suffix is ready before the program has established it.

Now branch at the allocation request. If it fails, `replacement` is null but the owning `data` field still refers to the original allocation. Length remains 60, capacity remains 64, and no suffix was copied. The caller can release that allocation or retry the append. The six source bytes remain the caller's responsibility in either case: append copies them; it never takes ownership of their array.

**Predict:** a caller saved a pointer to offset 57 before the append. What happens to that borrow on allocation failure, on successful growth, and on a three-byte append that fits without growth? How can the caller remember the same position across successful growth?

:::reveal Review the append trace

Allocation failure preserves the old allocation, so the pointer to offset 57 remains usable within its original bounds. Successful growth through `realloc` ends the old allocation's lifetime; reconstruct the pointer as `b.data + 57` using the current owner. The integer offset survives relocation because it describes a position in the preserved prefix.

A three-byte append reaches length 63 without calling `realloc`; capacity stays 64 and the existing borrow remains valid. This conclusion follows from the reserve function's early return, not from assuming that allocations usually stay at the same address. Destruction ends every remaining borrow.

:::

Now question a piece of work the reference performs: every input byte first enters `chunk`, then gets copied into the retained allocation. Withholding output requires retaining the bytes, but does it require that intermediate copy? A different collector could reserve writable space, read directly into the unused suffix, and advance `len` by the count actually obtained. The required storage remains; one copy of each input byte disappears from this data path.

That alternative has a cost of its own: input handling must now respect the buffer's capacity and lifetime rules directly. It must reserve before forming the destination pointer, request no more than the available space, and still discard collected data on a read failure. Before adding that complexity, measure whether copying from `chunk` is significant in the intended workload. Predict fewer explicitly copied bytes, then test elapsed time separately. A reduced operation count does not guarantee a useful speedup when another operation dominates.

## Test the boundaries you derived

```sh
mkdir -p build
cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -g examples/buffer.c -o build/buffer
printf 'hello\n' | ./build/buffer > build/buffer.out
cat build/buffer.out
./build/buffer < /dev/null > build/empty.out
printf 'A\000B\n' | ./build/buffer > build/binary.out
od -An -tx1 build/binary.out
```

Predict the logical lengths, including newlines, before checking the reported capacities on standard error. Output bytes and diagnostic capacity are different claims: the former is the tool's purpose, while the latter exposes this implementation's growth decisions.

A diagnostic build adds evidence about executed accesses:

```sh
cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -g -fsanitize=address,undefined -fno-omit-frame-pointer examples/buffer.c -o build/buffer-sanitize
printf 'memory experiment\n' | ./build/buffer-sanitize > build/sanitize.out
```

A clean run covers the paths executed, not every possible lifetime error. Assertions express internal assumptions and may be disabled; they do not replace checks on external sizes. To test allocation failure reproducibly, inject a failure through a wrapper around allocation rather than exhausting the host machine.

## Exercises and a changed storage budget

1. Feed 0, 1, 64, 65, and 129 bytes. Predict lengths and capacities, then instrument allocation requests in an experimental copy. Explain which observations test correctness and which merely describe the selected policy.
2. Add a 1 MiB input limit. State whether exactly 1 MiB is accepted, derive an overflow-safe limit check, and verify that rejection emits none of the buffered input to standard output.
3. Implement `buffer_append_slice(b, offset, length)` for appending part of the buffer to itself. State the valid original range, the moment addresses must be reconstructed, and whether the final copy ranges overlap. Cover empty slices and growth.

**Transfer task — change one assumption.** RAM is now limited to 16 KiB, while finite input may be arbitrarily large. The no-output-before-successful-collection requirement remains. Explain why a general solution cannot retain all required information in that memory alone. Compare relaxing output timing with permitting a temporary backing file. For the latter, identify the new ownership, failure, and replay obligations instead of assuming that disk storage is unlimited or infallible.

**Transfer to a worker service.** A valid per-job buffer limit does not bound the whole service. Suppose fixed state uses `F` bytes, queued input uses `Q`, and each of `k` active jobs reserves at most `R`. Under that model, require `F + Q + kR <= B` for memory budget `B`, with representability checks before doing the arithmetic in C. Which owner releases a reservation after failure? Can input queueing grow while workers are busy? Derive the admission decision before optimizing the allocator. [Appendix F](#appendix-f--from-c-systems-to-ai-infrastructure) develops this accounting for a simulated model-work scheduler.

**Write your version.** Give an argument for append safety in the order the implementation needs it: valid source, representable total size, adequate live allocation, valid destination range, copied suffix, committed length. Then identify the assumptions this argument does not establish, such as allocator availability and eventual EOF.

:::reveal Your review

A successful one-byte append at length 64 leaves length 65 and, under this policy, capacity 128. Allocation failure leaves pointer, length, capacity, and contents unchanged. A successful `realloc` invalidates use of old pointers even if the returned address has the same printed value.

An aliased source can become invalid during growth, before copying begins. `memmove` handles overlapping live ranges; it cannot revive an expired source pointer. An offset-based interface can validate the original slice and reconstruct its address from the new allocation.

The sample `hello` input contains six bytes and receives capacity 64. Empty input has zero length and capacity. Binary output is `41 00 42 0a`. With fresh buffers, the exercise lengths 0, 1, 64, 65, and 129 produce capacities 0, 64, 64, 128, and 256.

For the limit, maintain `len <= limit` and reject `n > limit - len` before appending. For a self-slice, require `offset <= old_len` and `length <= old_len - offset`; separately check the appended total for overflow. Reserve, reconstruct the source, then copy to `data + old_len`. The original slice ends no later than the destination begins, so positive-length ranges do not overlap. Return before pointer arithmetic for a zero-length slice.

Withholding input bytes until collection succeeds does not make final output transactional. A later write can expose a prefix and then fail. The 16 KiB transfer task requires a changed resource or timing assumption; a different growth factor cannot solve the information-storage requirement.

:::
