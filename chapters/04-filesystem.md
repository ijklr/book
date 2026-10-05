# Chapter 4 — Names and filesystem objects

A pathname looks like the identity of a file because we use names to discuss files. The operating system's model is more subtle. A name is part of a directory structure that can change. An open descriptor refers to an object obtained through an earlier operation. Understanding that distinction explains hard links, symbolic links, renaming, and a large class of race conditions.

Our `fileinfo` tool reports metadata for one pathname. It distinguishes a symbolic link from its target and lists the immediate names inside a directory. It does not create, remove, or rename entries, or write file contents. Observation can still affect metadata: reading a directory can update its access timestamp. [POSIX directory-read semantics](https://man7.org/linux/man-pages/man3/readdir.3p.html)

## Separate a name from an object

A regular file contains bytes and associated metadata. A directory maps names to filesystem objects. The pathname `notes/chapter.txt` requests a sequence of lookups beginning at the current directory. An absolute pathname begins at the root directory of the process's filesystem view instead. `.` and `..` participate in pathname resolution; a pathname is more than a string with slashes removed.

A hard link adds another directory entry referring to the same object. Neither name is inherently the original. Removing one name does not remove the other. A symbolic link is a separate object containing a target pathname. Its target can be absent. When a relative symbolic-link target is followed, its interpretation begins at the directory containing the link, not at the caller's current directory.

The device and inode fields together help identify filesystem objects within the relevant filesystem view at an observation point. An inode number alone is insufficient across filesystems, and inode numbers can be reused over time. Treat a captured number as diagnostic information, not a durable global identifier.

## Ask the right metadata question

`stat(path, ...)` normally follows the final symbolic link and reports the target. `lstat(path, ...)` reports the final link itself when that component is a symbolic link. `fstat(fd, ...)` reports the object associated with an open descriptor. The [stat family manual](https://man7.org/linux/man-pages/man2/stat.2.html) documents these distinctions and metadata fields.

The word “final” matters. `lstat` does not prohibit symbolic links in earlier pathname components. Nor is it a complete defense against concurrent pathname changes. It answers a particular metadata question at a particular operation.

The program initially calls `lstat`. For a symbolic link, it also calls `stat` and labels the two results separately. For a directory, it opens a directory stream and calls `fstat` on that stream's descriptor. That second result describes the directory actually opened, even if the pathname changed after the initial lookup.

Metadata is interpreted according to object type. For a regular file, `st_size` is the logical byte length. A directory's size is not its number of entries. Use `S_ISREG`, `S_ISDIR`, and related predicates to inspect `st_mode`; do not compare the entire mode value to a file-type constant, because the value also includes other bits.

{{include:examples/fileinfo.c}}

## Directory iteration is a separate protocol

`opendir` creates a `DIR *` stream. Repeated `readdir` calls yield entries. The returned pointer is borrowed from the stream implementation; consume or copy the name before later operations invalidate or replace it. Finish with `closedir`, which owns the underlying directory descriptor. Do not separately close the result of `dirfd` while the stream owns it.

The null result from `readdir` has two meanings: end of directory or failure. Set `errno` to zero immediately before the call, then inspect it only if the result is null. That pattern belongs to this interface; it is not a general reason to inspect `errno` after every successful function. The [readdir manual](https://man7.org/linux/man-pages/man3/readdir.3.html) describes the distinction and the returned entry's lifetime.

Directory order is unspecified for our purposes. It may look stable during repeated runs, but the tool makes no sorting promise. The program also does not use `d_type`: applications that need reliable object metadata should obtain it through the appropriate metadata operation instead of assuming every filesystem supplies a useful type in every directory entry.

The listing skips `.` and `..`, and it does not recurse. A recursive walker needs policy for symbolic links, cycles, permission failures, directory replacement, and resource limits. Adding a recursive call without answering those questions is not a complete traversal design.

Names can contain spaces, newlines, and bytes your terminal interprets as commands. The listing therefore quotes each name and escapes bytes outside a small printable set. Non-ASCII names remain represented by their bytes, not converted into a claim about Unicode characters. This format is for inspection; it is not a shell command to execute.

## Build an experiment directory

These commands use a freshly generated directory so they do not overwrite existing files:

```sh
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

## Exercises

1. Append one byte through `alias` in the experiment directory. Inspect `original` and `shortcut` again. Predict which metadata sizes and contents change before running the commands.
2. Extend the output to show owner, group, and permission bits. Use types and formatting that match the interface, and separate the file-type bits from the permission bits.
3. Add an option that lists regular files only. Obtain metadata relative to the opened directory using `fstatat` and decide whether to follow final symbolic links. Document what happens when an entry disappears between iteration and inspection.

**Write your version.** Draw the directory entries for the two hard links, the symbolic-link object, and the shared regular file. Then write a second paragraph explaining why an open descriptor and a pathname answer different questions after a rename.

## Selected answers

In exercise 1, `printf 'd' >> "$lab_dir/alias"` changes the shared regular file to four bytes. Both hard-link names and the symbolic link's target observe those contents. The symbolic link object's own stored target remains `original`; its own size does not become four.

For exercise 2, mask ordinary permission bits with `0777` if those nine bits are what your output promises. Special permission bits require an explicit additional policy. For exercise 3, `fstatat(dirfd(dir), entry->d_name, &st, AT_SYMLINK_NOFOLLOW)` inspects the final entry without following its final symbolic link. Include `<fcntl.h>` for the flag. Check failure normally: an entry returned earlier is not a promise that its name still exists, and concurrent replacement can change which object it resolves to.
