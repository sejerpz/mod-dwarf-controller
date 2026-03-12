#include <sys/stat.h>
#include <errno.h>

#ifndef ENABLE_SEMIHOST
#pragma message "using syscall stubs (nosys)"
int _close(int fd)                          { (void)fd; return -1; }
int _fstat(int fd, struct stat *st)         { (void)fd; (void)st; return -1; }
int _getpid(void)                           { return 1; }
int _isatty(int fd)                         { (void)fd; return 1; }
int _kill(int pid, int sig)                 { (void)pid; (void)sig; return -1; }
int _lseek(int fd, int offset, int whence)  { (void)fd; (void)offset; (void)whence; return -1; }
int _read(int fd, char *buf, int len)       { (void)fd; (void)buf; (void)len; return -1; }
int _write(int fd, char *buf, int len)      { (void)fd; (void)buf; (void)len; return len; }

void *_sbrk(ptrdiff_t incr) {
    (void)incr;
    errno = ENOMEM;
    return (void *)-1;
}
#else
#pragma message "using rdimon semihosting"
#endif