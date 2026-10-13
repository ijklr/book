# Chapter 4 — Names and filesystem objects

A pathname looks like the identity of a file because we use names to discuss files. The operating system's model is more subtle. A name is part of a directory structure that can change. An open descriptor refers to an object obtained through an earlier operation. Understanding that distinction explains hard links, symbolic links, renaming, and a large class of race conditions.

Design `fileinfo PATH` to report metadata for one pathname. If the initial lookup identifies a symbolic link, report metadata for both the link and its target. If it identifies a directory, list that directory's immediate entry names. For `fileinfo link`, a symbolic link to a directory receives the two metadata reports but no listing. This policy separates inspecting a link's target from traversing it.

The tool returns 0 after completing the requested inspection, 1 for a detected operational failure, and 2 for invalid arguments. It does not create, remove, or rename entries, or write file contents. Observation can still affect metadata: reading a directory can update its access timestamp. [POSIX directory-read semantics](https://man7.org/linux/man-pages/man3/readdir.3p.html)

## Separate a name from an object

A regular file contains bytes and associated metadata. A directory maps names to filesystem objects. The pathname `notes/chapter.txt` requests a sequence of lookups beginning at the current directory. An absolute pathname begins at the root directory of the process's filesystem view instead. `.` and `..` participate in pathname resolution; a pathname is more than a string with slashes removed.

A hard link adds another directory entry referring to the same object. Neither name is inherently the original. Removing one name does not remove the other. A symbolic link is a separate object containing a target pathname. Its target can be absent. When a relative symbolic-link target is followed, its interpretation begins at the directory containing the link, not at the caller's current directory.

The device and inode fields together help identify filesystem objects within the relevant filesystem view at an observation point. An inode number alone is insufficient across filesystems, and inode numbers can be reused over time. Treat a captured number as diagnostic information, not a durable global identifier.

## Ask the right metadata question

Choose the operation from the object you mean to inspect:

| Question | Operation |
| --- | --- |
| What does this pathname resolve to, following its final symbolic link? | `stat(path, ...)` |
| What does this pathname name, without following a final symbolic link? | `lstat(path, ...)` |
| What object does this open descriptor refer to? | `fstat(fd, ...)` |

The [stat family manual](https://man7.org/linux/man-pages/man2/stat.2.html) documents these distinctions and metadata fields.

The word “final” matters. `lstat` does not prohibit symbolic links in earlier pathname components. Nor is it a complete defense against concurrent pathname changes. It answers a particular metadata question at a particular operation.

The program initially calls `lstat`. For a symbolic link, it also calls `stat` and labels the two results separately. For a directory, it opens a directory stream and calls `fstat` on that stream's descriptor. That second result describes the directory actually opened, even if the pathname changed after the initial lookup.

Metadata is interpreted according to object type. For a regular file, `st_size` is the logical byte length. A directory's size is not its number of entries. Use `S_ISREG`, `S_ISDIR`, and related predicates to inspect `st_mode`; do not compare the entire mode value to a file-type constant, because the value also includes other bits.

## Directory iteration is a separate protocol

`opendir` creates a directory stream, represented by a `DIR *`. Repeated `readdir` calls yield entries. The returned pointer is borrowed from the stream implementation; consume or copy the name before the next `readdir` on that stream can overwrite it, and before closing the stream. Saving the pointer alone does not save the name. The stream owns the underlying directory descriptor, and `closedir` releases both. Do not separately close the result of `dirfd` while the stream owns it.

The null result from `readdir` has two meanings: end of directory or failure. Set `errno` to zero immediately before the call, then inspect it only if the result is null. That pattern belongs to this interface; it is not a general reason to inspect `errno` after every successful function. The [readdir manual](https://man7.org/linux/man-pages/man3/readdir.3.html) describes the distinction and the returned entry's lifetime.

Directory order is unspecified for our purposes. It may look stable during repeated runs, but the tool makes no sorting promise. The program also does not use `d_type`: applications that need reliable object metadata should obtain it through the appropriate metadata operation instead of assuming every filesystem supplies a useful type in every directory entry.

The listing skips `.` and `..`, and it does not recurse. A recursive walker needs policy for symbolic links, cycles, permission failures, directory replacement, and resource limits. Adding a recursive call without answering those questions is not a complete traversal design.

Names can contain spaces, newlines, and control sequences interpreted by a terminal. The listing therefore quotes each name and escapes bytes outside a small printable set. Non-ASCII names remain represented by their bytes; the program does not decode them as Unicode. This format is for inspection; it is not a shell command to execute.

## Derive it yourself

Before opening the reference, draw the decision tree for a regular file, directory, symbolic link to a file, symbolic link to a directory, and dangling symbolic link. For each case, identify the metadata operations and whether directory iteration runs. Which failure can occur after some normal output has already been printed?

Then trace resource ownership through `opendir`, `fstat`, `readdir`, and `closedir`. Once `opendir` succeeds, every later exit path must attempt to release that stream. A failed metadata lookup does not remove that obligation. Decide whether retaining all entry names serves any requirement of this unsorted, nonrecursive listing.

:::reveal Reference implementation

{{include:examples/fileinfo.c}}

:::

## Build an experiment directory

These commands use a freshly generated directory so they do not overwrite existing files:

```sh
mkdir -p build
cc -std=c17 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -Werror -g examples/fileinfo.c -o build/fileinfo
lab_dir=$(mktemp -d)
printf 'abc' > "$lab_dir/original"
ln "$lab_dir/original" "$lab_dir/alias"
ln -s original "$lab_dir/shortcut"
ln -s absent "$lab_dir/broken"
./build/fileinfo "$lab_dir/original"
./build/fileinfo "$lab_dir/alias"
./build/fileinfo "$lab_dir/shortcut"
./build/fileinfo "$lab_dir"
```

The two hard-link names should report the same device and inode, link count two, and size three. The symbolic link's `name` line reports a link object; its `target` line reports the regular file. The directory invocation prints metadata followed by the immediate entry names, in an order you should not rely on.

Now inspect the dangling link:

```sh
./build/fileinfo "$lab_dir/broken"
printf 'status=%s\n' "$?"
```

The link itself exists, so its metadata can be printed. Looking up its target fails, and this tool returns status 1. That policy is explicit: “the link exists” and “the target can be resolved” are different questions, and the requested inspection includes both.

The temporary directory is left available for further experiments. Record its pathname in your notebook and remove it when you have finished, using the ordinary file-management method you prefer.

## Observing a path does not reserve it

Suppose a program checks a pathname with `stat`, decides the file is acceptable, then opens that pathname. Another process can replace a directory entry between those operations. The open may reach a different object. This is a time-of-check/time-of-use race: the check and use do not act on a single stable reference.

For ordinary inspection, our tool can report observations made at different times. It does not claim to produce a transactional snapshot. For an operation that must validate the object it will actually use, opening first and applying `fstat` to the resulting descriptor is often the right starting point. Requirements about allowed paths and symlinks can require additional descriptor-relative APIs and platform-specific controls; `fstat` alone does not establish every security property.

A descriptor stabilizes the reference, not the file's contents. Another writer may still change bytes or metadata. Likewise, directory contents can change while being listed. Concurrency requires asking exactly which property must stay stable, for how long, and which operation provides that guarantee.

Consider a proposed fix: “Check the pathname twice before opening it.” Derive its effect using a schedule rather than hoping repeated checks are reassuring. At time one, the name refers to object A and the first check succeeds. At time two, the second check also sees A. At time three, another process replaces the name with one referring to B. At time four, the open obtains B. Increasing the number of checks only moves the last opportunity for replacement; it never joins the check and use into one operation.

For the narrower requirement “report metadata for the object I opened,” derive the operations from that sentence: open, then inspect the descriptor. A separate preliminary pathname check adds no guarantee of that identity. Test the distinction with a controlled pause between lookup and open, replacing the name during the pause. One forced counterexample disproves the repeated-check argument more directly than thousands of runs in which the timing happens to cooperate.

## Keep an object open while its name changes

Make the distinction observable without relying on a race. In a separate scratch directory, create two regular files with different contents. The parenthesized commands run in a subshell, so their saved descriptor is local to this experiment:

```sh
rename_dir=$(mktemp -d)
printf 'old\n' > "$rename_dir/current"
printf 'new\n' > "$rename_dir/replacement"
(
    exec 3< "$rename_dir/current"
    mv "$rename_dir/replacement" "$rename_dir/current"
    printf 'saved descriptor: '
    cat <&3
    printf 'new pathname: '
    cat "$rename_dir/current"
    exec 3<&-
)
```

Here `exec` has no program argument: it establishes descriptor 3 in the subshell. `cat <&3` reads through that saved connection; `cat "$rename_dir/current"` opens the pathname again. Both files are in the same directory, so the replacement is a rename within one filesystem. Assume no other process changes these files and each operation succeeds.

Call the original object A and the replacement B. Opening `current` first obtains a reference to A. Renaming then makes `current` name B and removes A's directory entry. The existing descriptor still refers to A; it is not a promise to repeat the lookup of `current` before each read. POSIX keeps an unlinked object's contents available while open references remain. [Rename semantics](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rename.html)

The two lines therefore report `old` and `new`. A later `fileinfo "$rename_dir/current"` inspects B. An `fstat` on saved descriptor 3 would inspect A. This is the same distinction behind the example's separate `name` and `opened-directory` metadata reports.

**Predict:** insert a second `cat <&3` immediately after the first. Does it read `old` again, read `new`, or print no bytes? Explain both which object it reaches and where reading begins.

:::reveal Review the saved descriptor

The second read prints no bytes. The first `cat` has advanced the shared open file description's offset to EOF in A. Reusing the descriptor preserves both its object reference and that offset; it does not reopen A at the beginning. A fresh open of `current` reaches B at its own initial offset.

Closing descriptor 3 releases the remaining saved reference. The rename changed a directory entry, while reads changed an offset. Keeping these transitions separate explains the result without assuming that a pathname permanently identifies an object.

:::

## Exercises

1. Append one byte through `alias` in the experiment directory. Inspect `original` and `shortcut` again. Predict which metadata sizes and contents change before running the commands.
2. Extend the output to show owner, group, and permission bits. Use types and formatting that match the interface, and separate the file-type bits from the permission bits.
3. Add an option that lists regular files only. Obtain metadata relative to the opened directory using `fstatat` and decide whether to follow final symbolic links. Document what happens when an entry disappears between iteration and inspection.

**Write your version.** Draw the directory entries for the two hard links, the symbolic-link object, and the shared regular file. Then write a second paragraph explaining why an open descriptor and a pathname answer different questions after a rename.

## Selected answers

:::reveal Review after your attempt

Every valid invocation begins with `lstat`. A regular file needs no further metadata lookup. A symbolic link adds `stat`, even when its target is a directory; a missing target causes failure after the link's own metadata has been printed. A directly named directory adds `opendir`, `fstat` on the opened descriptor, iteration, and `closedir`. The streaming listing needs only the current borrowed entry; sorting would introduce a reason to retain names.

In exercise 1, `printf 'd' >> "$lab_dir/alias"` changes the shared regular file to four bytes. Both hard-link names and the symbolic link's target observe those contents. The symbolic link object's own stored target remains `original`; its own size does not become four.

For exercise 2, mask ordinary permission bits with `0777` if those nine bits are what your output promises. Special permission bits require an explicit additional policy. For exercise 3, `fstatat(dirfd(dir), entry->d_name, &st, AT_SYMLINK_NOFOLLOW)` inspects the final entry without following its final symbolic link. Include `<fcntl.h>` for the flag. Check failure normally: an entry returned earlier is not a promise that its name still exists, and concurrent replacement can change which object it resolves to.

:::
