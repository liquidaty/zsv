#ifndef ZSV_THREAD_H
#define ZSV_THREAD_H
/*
 * for now we don't really need thread support because this is only being used
 * by the CLI. However, it's here anyway in case future enhancements or
 * user customizations need multithreading support
 */
#ifndef ZSVTLS
# ifndef NO_THREADING
#  define ZSVTLS _Thread_local
# else
#  define ZSVTLS
# endif
#endif

#endif
