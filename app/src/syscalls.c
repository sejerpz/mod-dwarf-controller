#include <sys/stat.h>
#include <errno.h>
#include <string.h>
#include <reent.h>
#include "FreeRTOS.h"

/* newlib allocator on the FreeRTOS heap
 *
 * newlib itself allocates (strtod bigints, stdio), and with the nosys _sbrk below its own
 * malloc always fails: strtod then asserts and the controller hangs. Routing it to
 * pvPortMalloc gives a single heap, already thread safe, so no __malloc_lock is needed.
 */

/* heap_5 keeps this header right before every block it hands out */
typedef struct heap_block_header {
    void *next_free_block;
    size_t block_size;
} heap_block_header_t;

#define HEAP_HEADER_SIZE    ((sizeof(heap_block_header_t) + (portBYTE_ALIGNMENT - 1)) & ~((size_t)portBYTE_ALIGNMENT_MASK))
#define HEAP_ALLOCATED_BIT  ((size_t)1 << ((sizeof(size_t) * 8) - 1))

static size_t heap_usable_size(void *ptr)
{
    heap_block_header_t *header = (heap_block_header_t *)((uint8_t *)ptr - HEAP_HEADER_SIZE);
    return (header->block_size & ~HEAP_ALLOCATED_BIT) - HEAP_HEADER_SIZE;
}

void *_malloc_r(struct _reent *r, size_t size)
{
    void *ptr = pvPortMalloc(size);
    if (!ptr) r->_errno = ENOMEM;
    return ptr;
}

void _free_r(struct _reent *r, void *ptr)
{
    (void)r;
    vPortFree(ptr);
}

void *_calloc_r(struct _reent *r, size_t count, size_t size)
{
    size_t total = count * size;
    if (size && (total / size != count)) {
        r->_errno = ENOMEM;
        return NULL;
    }

    void *ptr = _malloc_r(r, total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

void *_realloc_r(struct _reent *r, void *ptr, size_t size)
{
    if (!ptr) return _malloc_r(r, size);
    if (!size) {
        vPortFree(ptr);
        return NULL;
    }

    size_t old_size = heap_usable_size(ptr);
    if (size <= old_size) return ptr;

    void *new_ptr = _malloc_r(r, size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, old_size);
        vPortFree(ptr);
    }
    return new_ptr;
}

void *malloc(size_t size)                   { return _malloc_r(_REENT, size); }
void free(void *ptr)                        { _free_r(_REENT, ptr); }
void *calloc(size_t count, size_t size)     { return _calloc_r(_REENT, count, size); }
void *realloc(void *ptr, size_t size)       { return _realloc_r(_REENT, ptr, size); }

/* Support for librdimon JTAG Debugger */
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