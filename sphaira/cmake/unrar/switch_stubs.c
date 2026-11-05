// POSIX calls that UnRAR links against but newlib / libnx do not provide.
// UnRAR only reaches them when it writes entries to disk itself (owners, symlinks,
// file times, update-mode locks). Kefir Hub never asks for that: it reads entry data
// through the RAR_TEST + UCM_PROCESSDATA callback and writes files itself.
#include <errno.h>
#include <stddef.h>
#include <sys/types.h>

struct passwd;
struct group;
struct timespec;

int lchown(const char* path, uid_t owner, gid_t group) {
    (void)path; (void)owner; (void)group;
    errno = ENOSYS;
    return -1;
}

struct passwd* getpwnam(const char* name) {
    (void)name;
    return NULL;
}

struct group* getgrnam(const char* name) {
    (void)name;
    return NULL;
}

uid_t geteuid(void) {
    return 0;
}

int flock(int fd, int operation) {
    (void)fd; (void)operation;
    return 0;
}

int utimensat(int dirfd, const char* path, const struct timespec times[2], int flags) {
    (void)dirfd; (void)path; (void)times; (void)flags;
    errno = ENOSYS;
    return -1;
}
