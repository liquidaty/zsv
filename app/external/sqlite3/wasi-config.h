#define LONGDOUBLE_TYPE                 double
#define SQLITE_BYTEORDER                1234
#define NDEBUG                          1
#define SQLITE_OS_UNIX                  1
#define SQLITE_DISABLE_LFS              1
#define SQLITE_ENABLE_JSON1             1
#define SQLITE_HAVE_ISNAN               1
#define SQLITE_HAVE_MALLOC_USABLE_SIZE  1
#define SQLITE_HAVE_STRCHRNUL           1
#define SQLITE_LIKE_DOESNT_MATCH_BLOBS  1
// #define SQLITE_OMIT_DECLTYPE            1
#define SQLITE_OMIT_DEPRECATED          1
#define SQLITE_OMIT_LOAD_EXTENSION      1
#define SQLITE_TEMP_STORE               2
#define SQLITE_THREADSAFE               0
#define SQLITE_USE_URI                  1
#define SQLITE_ENABLE_RTREE             1
#define SQLITE_ENABLE_FTS5              1
#define SQLITE_HAVE_USLEEP              1
#define SQLITE_ENABLE_EXPLAIN_COMMENTS  1
#undef HAVE_FCHOWN
typedef unsigned uid_t;
typedef uid_t gid_t;
#ifndef FCHOWN_DEFINED
int fchown(int fd, uid_t owner, gid_t group);
#define FCHOWN_DEFINED
int fchown(int fd, uid_t owner, gid_t group){
  return (fd && owner && group) ? 0 : 0;
}
#endif

#ifdef __wasi__
typedef unsigned mode_t;
int fchmod(int fd, mode_t mode);
typedef unsigned uid_t;
typedef uid_t gid_t;
int fchown(int fd, uid_t owner, gid_t group);
uid_t geteuid(void);
uid_t geteuid(void){return 0;}

#define F_RDLCK 0
#define F_WRLCK 1
#define F_UNLCK 2
#if __LONG_MAX == 0x7fffffffL
#define F_GETLK 12
#define F_SETLK 13
#define F_SETLKW 14
#else
#define F_GETLK 5
#define F_SETLK 6
#define F_SETLKW 7
#endif
#endif
