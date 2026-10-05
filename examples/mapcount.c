#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int parse_byte(const char *text, unsigned char *byte)
{
    if (*text == '\0') {
        return -1;
    }
    for (const char *p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return -1;
        }
    }
    errno = 0;
    char *end;
    unsigned long value = strtoul(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || value > 255) {
        return -1;
    }
    *byte = (unsigned char)value;
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char wanted;
    if (argc != 3 || parse_byte(argv[2], &wanted) == -1) {
        fprintf(stderr, "usage: %s FILE BYTE (decimal 0..255)\n", argv[0]);
        return 2;
    }
    int fd;
    do {
        fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    } while (fd == -1 && errno == EINTR);
    if (fd == -1) {
        perror("open");
        return 1;
    }
    struct stat info;
    int result;
    do {
        result = fstat(fd, &info);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        perror("fstat");
        (void)close(fd);
        return 1;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0 ||
        (uintmax_t)info.st_size > (uintmax_t)PTRDIFF_MAX ||
        (uintmax_t)info.st_size > (uintmax_t)SIZE_MAX) {
        fprintf(stderr, "mapcount: requires a regular file of mappable size\n");
        (void)close(fd);
        return 1;
    }

    size_t length = (size_t)info.st_size;
    size_t count = 0;
    if (length != 0) {
        void *mapping = mmap(NULL, length, PROT_READ, MAP_PRIVATE, fd, 0);
        if (mapping == MAP_FAILED) {
            perror("mmap");
            (void)close(fd);
            return 1;
        }
        if (close(fd) == -1) {
            perror("close");
            (void)munmap(mapping, length);
            return 1;
        }
        const unsigned char *bytes = mapping;
        for (size_t i = 0; i < length; ++i) {
            if (bytes[i] == wanted) {
                ++count;
            }
        }
        if (munmap(mapping, length) == -1) {
            perror("munmap");
            return 1;
        }
    } else if (close(fd) == -1) {
        perror("close");
        return 1;
    }

    if (printf("%zu\n", count) < 0 || fflush(stdout) == EOF) {
        perror("stdout");
        return 1;
    }
    return 0;
}
