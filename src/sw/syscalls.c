// The system calls under newlib-nano (snprintf and friends): a heap in the free SRAM, no files.
#include <stddef.h>
#include <errno.h>
#include <sys/stat.h>
#include "mmio.h"

extern char __heap_start[], __heap_end[];   // link.ld: the program, the heap, the stack below the sounds
static char *brk = __heap_start;

void *_sbrk(ptrdiff_t n) {
  char *p = brk;
  if (brk + n > __heap_end) { errno = ENOMEM; return (void *) -1; }
  brk += n;
  return p;
}
int _write(int fd, const char *b, int n) { (void) fd; for (int i = 0; i < n; i++) R_DBG = (uint8_t) b[i]; return n; }
int _read(int fd, char *b, int n) { (void) fd; (void) b; (void) n; return 0; }
int _close(int fd) { (void) fd; return -1; }
int _fstat(int fd, struct stat *st) { (void) fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void) fd; return 1; }
int _lseek(int fd, int off, int w) { (void) fd; (void) off; (void) w; return 0; }
void _exit(int c) { (void) c; for (;;) ; }
int _kill(int pid, int sig) { (void) pid; (void) sig; errno = EINVAL; return -1; }
int _getpid(void) { return 1; }
