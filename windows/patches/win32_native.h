/*
 *  win32_native.h
 *
 *  POSIX compatibility layer for building Ciao Prolog on native Windows.
 *
 *  Included from eng.h when WIN32_NATIVE is defined. Provides:
 *    - Windows SDK headers
 *    - POSIX constants (STDIN_FILENO, F_OK, signal numbers, etc.)
 *    - POSIX types for non-MinGW compilers (pid_t, uid_t, ...)
 *    - Declarations for Win32 replacements (windows/patches/*.c)
 *    - Macro redirects so engine code compiles without #ifdef changes
 *
 *  The build script copies this to include/ciao/win32_native.h.
 */

#ifndef _CIAO_WIN32_NATIVE_H
#define _CIAO_WIN32_NATIVE_H

#if defined(WIN32_NATIVE) || (defined(_WIN32) && !defined(__CYGWIN__) && !defined(__MSYS__))

/* --- Windows headers --- */

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <fcntl.h>
#include <signal.h>

/* --- POSIX constants --- */

#ifndef STDIN_FILENO
#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#ifndef F_OK
#define F_OK 0
#define R_OK 4
#define W_OK 2
#define X_OK 0  /* not meaningful on Windows */
#endif

/*
 * POSIX-to-CRT name mappings (MSVC only).
 * MinGW provides POSIX names natively; these are only needed for MSVC.
 *
 * NOTE: Do NOT define `access` as a macro -- eng_errcodes.h uses it
 * as a token-paste argument in ERR_permission_error.
 */
#if defined(_MSC_VER) && !defined(__MINGW32__)
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

/* Signal numbers not defined by the Windows CRT.
 * Standard POSIX values; delivery is handled by signals_win32.c. */

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

/* Process status macros.
 * Windows exit codes are plain integers; no signal-termination concept. */

#ifndef WIFEXITED
#define WIFEXITED(x)   1
#define WEXITSTATUS(x) ((x) & 0xFF)
#define WIFSIGNALED(x) 0
#define WTERMSIG(x)    0
#define WNOHANG        1
#endif

/* --- POSIX types (non-MinGW compilers only) ---
 * MinGW-w64 provides these via sys/types.h. */

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

/* --- Path separators --- */

#ifndef PATHSEP
#define PATHSEP "\\"
#endif
#ifndef PATHLISTSEP
#define PATHLISTSEP ";"
#endif

/* --- Win32 replacement function declarations ---
 * Implemented in windows/patches/*.c, compiled alongside the engine. */

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

/* --- POSIX-to-Win32 macro redirects ---
 * Transparent redirection so engine .c files compile without changes. */

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
