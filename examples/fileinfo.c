#include <dirent.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *kind(mode_t mode)
{
    if (S_ISREG(mode)) return "regular";
    if (S_ISDIR(mode)) return "directory";
    if (S_ISLNK(mode)) return "symlink";
    if (S_ISFIFO(mode)) return "fifo";
    if (S_ISSOCK(mode)) return "socket";
    if (S_ISCHR(mode)) return "character-device";
    if (S_ISBLK(mode)) return "block-device";
    return "other";
}

static void show_stat(const char *label, const struct stat *st)
{
    printf("%s: type=%s device=%ju inode=%ju links=%ju size=%jd\n",
           label, kind(st->st_mode), (uintmax_t)st->st_dev,
           (uintmax_t)st->st_ino, (uintmax_t)st->st_nlink,
           (intmax_t)st->st_size);
}

/* Escape bytes so newline and terminal-control bytes stay visible. */
static void show_name(const char *name)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) {
        if (*p >= 32 && *p <= 126 && *p != '\\' && *p != '"') {
            putchar(*p);
        } else {
            printf("\\x%02x", (unsigned int)*p);
        }
    }
    puts("\"");
}

static int list_directory(const char *path)
{
    DIR *dir = opendir(path);
    if (dir == NULL) {
        perror("fileinfo: opendir");
        return 1;
    }
    int result = 0;
    struct stat opened;
    if (fstat(dirfd(dir), &opened) == -1) {
        perror("fileinfo: fstat");
        result = 1;
    } else {
        show_stat("opened-directory", &opened);
        for (;;) {
            errno = 0;
            struct dirent *entry = readdir(dir);
            if (entry == NULL) {
                if (errno != 0) {
                    perror("fileinfo: readdir");
                    result = 1;
                }
                break;
            }
            if (strcmp(entry->d_name, ".") != 0 &&
                strcmp(entry->d_name, "..") != 0) {
                show_name(entry->d_name);
            }
        }
    }
    if (closedir(dir) == -1) {
        perror("fileinfo: closedir");
        result = 1;
    }
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: fileinfo PATH\n");
        return 2;
    }
    struct stat named;
    if (lstat(argv[1], &named) == -1) {
        perror("fileinfo: lstat");
        return 1;
    }
    show_stat("name", &named);
    int result = 0;
    if (S_ISLNK(named.st_mode)) {
        struct stat target;
        if (stat(argv[1], &target) == -1) {
            perror("fileinfo: stat target");
            result = 1;
        } else {
            show_stat("target", &target);
        }
    } else if (S_ISDIR(named.st_mode)) {
        result = list_directory(argv[1]);
    }
    if (fflush(stdout) == EOF || ferror(stdout)) {
        perror("fileinfo: stdout");
        result = 1;
    }
    return result;
}
