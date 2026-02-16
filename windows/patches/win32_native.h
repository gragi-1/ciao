/*
 *  win32_native.h
 *
 *  Master include for Windows native build of Ciao Prolog engine.
 *  Include this from eng.h when WIN32_NATIVE is defined.
 *
 *  This header pulls in all Windows-specific compatibility
 *  definitions, replacing POSIX calls with Win32 API equivalents.
 */

#ifndef _CIAO_WIN32_NATIVE_H
#define _CIAO_WIN32_NATIVE_H

#if defined(WIN32_NATIVE) || (defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__))

/* ---- Windows headers ---- */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <fcntl.h>
#include <signal.h>

/* ---- POSIX compatibility defines ---- */

/* File descriptor operations */
#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

/* Access mode flags */
#ifndef F_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 0  /* Not meaningful on Windows */
#endif

/* Map POSIX names to Windows CRT names.
 * NOTE: MinGW-w64 provides most POSIX names natively (access, getcwd, etc.)
 * so we only define mappings for compilers that truly lack them (e.g. MSVC).
 * IMPORTANT: Do NOT define 'access' as a macro -- it conflicts with
 * token-pasting in eng_errcodes.h (ERR_permission_error uses 'access'
 * as a paste argument, producing _permission_perm__access). */
#if defined(_MSC_VER) && !defined(__MINGW32__)
/* MSVC-only POSIX compat mappings */
#ifndef getcwd
#define getcwd    _getcwd
#endif
#ifndef chdir
#define chdir     _chdir
#endif
#ifndef rmdir
#define rmdir     _rmdir
#endif
#ifndef unlink
#define unlink    _unlink
#endif
#ifndef fileno
#define fileno    _fileno
#endif
#ifndef isatty
#define isatty    _isatty
#endif
#ifndef dup
#define dup       _dup
#endif
#ifndef dup2
#define dup2      _dup2
#endif
#ifndef open
#define open      _open
#endif
#ifndef close
#define close     _close
#endif
#ifndef read
#define read      _read
#endif
#ifndef write
#define write     _write
#endif
#ifndef lseek
#define lseek     _lseek
#endif
#ifndef getpid
#define getpid    _getpid
#endif
#ifndef popen
#define popen     _popen
#endif
#ifndef pclose
#define pclose    _pclose
#endif
#ifndef snprintf
#define snprintf  _snprintf
#endif
#endif /* _MSC_VER && !__MINGW32__ */
#ifndef strcasecmp
#define strcasecmp _stricmp
#endif
#ifndef strncasecmp
#define strncasecmp _strnicmp
#endif

/* Signal numbers not defined in MinGW CRT */
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGUSR1
#define SIGUSR1 30
#endif
#ifndef SIGUSR2
#define SIGUSR2 31
#endif
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGBUS
#define SIGBUS  7
#endif
#ifndef SIGQUIT
#define SIGQUIT 3
#endif
#ifndef SIGHUP
#define SIGHUP  1
#endif

/* Process status macros */
#ifndef WIFEXITED
#define WIFEXITED(x)   1
#define WEXITSTATUS(x) ((x) & 0xFF)
#define WIFSIGNALED(x) 0
#define WTERMSIG(x)    0
#define WNOHANG        1
#endif

/* ---- Types ---- */
/* MinGW-w64 already provides pid_t, uid_t, gid_t, mode_t, ssize_t
   via sys/types.h. Only define if truly missing (non-MinGW compilers). */
#if !defined(__MINGW32__) && !defined(__MINGW64__)
#ifndef _PID_T_DEFINED
typedef int pid_t;
#define _PID_T_DEFINED
#endif
#ifndef _UID_T_DEFINED
typedef unsigned int uid_t;
#define _UID_T_DEFINED
#endif
#ifndef _GID_T_DEFINED
typedef unsigned int gid_t;
#define _GID_T_DEFINED
#endif
#ifndef _MODE_T_DEFINED
typedef unsigned short mode_t;
#define _MODE_T_DEFINED
#endif
#ifndef _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#define _SSIZE_T_DEFINED
#endif
#endif /* !__MINGW32__ && !__MINGW64__ */

/* ---- Directory separator ---- */
#ifndef PATHSEP
#define PATHSEP "\\"
#endif
#ifndef PATHLISTSEP
#define PATHLISTSEP ";"
#endif

/* ---- Function declarations from windows/patches/ ---- */

/* io_basic_win32.c */
extern int win32_normalize_path(const char *posix_path, char *win_path, size_t bufsize);
extern int win32_to_posix_path(const char *win_path, char *posix_path, size_t bufsize);
extern int win32_stdin_ready(void);
extern int win32_console_init(void);
extern void win32_console_cleanup(void);

/* signals_win32.c */
typedef void (*win32_signal_handler_t)(int);
extern win32_signal_handler_t win32_signal(int sig, win32_signal_handler_t handler);
extern int win32_signals_init(void);
extern void win32_signals_cleanup(void);
extern unsigned int win32_alarm(unsigned int seconds);

/* process_win32.c */
extern int win32_fork_stub(void);
extern int win32_setsid_stub(void);

/* system_win32.c */
extern void *win32_shm_open(const char *name, size_t size, int create);
extern int win32_shm_close(const char *name);
extern int win32_mkstemp(char *tmpl);
extern int win32_readlink(const char *path, char *buf, size_t bufsize);
extern int win32_getuid(void);
extern int win32_getgid(void);
extern const char *win32_get_username(void);
extern const char *win32_get_homedir(void);
extern unsigned int win32_sleep(unsigned int seconds);
extern int win32_usleep(unsigned int usec);

/* ---- Redirect POSIX calls to Win32 implementations ---- */
#define fork()            win32_fork_stub()
#define setsid()          win32_setsid_stub()
#define alarm(s)          win32_alarm(s)
#define readlink(p,b,s)   win32_readlink(p,b,s)
#define mkstemp(t)        win32_mkstemp(t)
#define sync()            ((void)0)
#define getuid()          win32_getuid()
#define getgid()          win32_getgid()
#define geteuid()         win32_getuid()
#define getegid()         win32_getgid()

#endif /* WIN32_NATIVE */

#endif /* _CIAO_WIN32_NATIVE_H */
