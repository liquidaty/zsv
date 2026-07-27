/*
 * WASI compatibility shim for the vendored SQLite amalgamation.
 *
 * wasi-libc declares struct flock and fcntl(), but not the POSIX advisory
 * record-locking macros that sqlite3.c's unix VFS references, and its fcntl()
 * rejects the lock commands. Supply the macros (musl values, which do not
 * collide with the F_GETFD..F_SETFL that wasi-libc does implement) and route
 * fcntl() through a wrapper that treats locking as a no-op.
 *
 * No-op locking costs nothing for the single-instance case: POSIX advisory locks
 * exist to arbitrate between *processes*, and sqlite3.c already arbitrates
 * between connections within one instance via its own unixInodeInfo bookkeeping.
 * It does mean concurrent access to one database from several wasm instances,
 * or from a wasm instance and the host at once, is unsupported and would corrupt.
 *
 * Include before sqlite3.c. The <fcntl.h>/<unistd.h> includes below must precede
 * the #defines, so that the real declarations are seen before the names are
 * rewritten; sqlite3.c's own later includes of both are then guard no-ops.
 */

#ifndef SQLITE3_WASI_ZSV_H
#define SQLITE3_WASI_ZSV_H

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef F_GETLK
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#endif

#ifndef F_RDLCK
#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2
#endif

static int zsv_wasi_fcntl(int fd, int cmd, ...) {
  va_list ap;
  int rc = 0;
  va_start(ap, cmd);
  switch (cmd) {
  case F_GETLK: // report the region as unlocked
    va_arg(ap, struct flock *)->l_type = F_UNLCK;
    break;
  case F_SETLK:
  case F_SETLKW:
    break;
  case F_SETFD:
  case F_SETFL:
    rc = fcntl(fd, cmd, va_arg(ap, int));
    break;
  default: // F_GETFD, F_GETFL and anything else wasi-libc handles
    rc = fcntl(fd, cmd);
    break;
  }
  va_end(ap);
  return rc;
}

// must follow zsv_wasi_fcntl(), whose own calls are to the real fcntl()
#define fcntl zsv_wasi_fcntl

/*
 * wasi has no user or group ids. sqlite3.c hardwires HAVE_FCHOWN, and uses the
 * pair only to hand a journal file back to the invoking user when running
 * setuid -- which cannot arise here. A non-zero euid makes its robustFchown()
 * skip fchown() altogether, so the fchown stub exists only to be addressable.
 */
static uid_t zsv_wasi_geteuid(void) {
  return 1;
}

static int zsv_wasi_fchown(int fd, uid_t uid, gid_t gid) {
  (void)fd;
  (void)uid;
  (void)gid;
  errno = ENOSYS;
  return -1;
}

#define geteuid zsv_wasi_geteuid
#define fchown zsv_wasi_fchown

#endif
