/*
 *  os_signal.h
 *
 *  OS signal handling (compatibility)
 *
 *  See Copyright Notice in ciaoengine.pl
 */

#ifndef _CIAO_OS_SIGNAL_H
#define _CIAO_OS_SIGNAL_H

#include <signal.h>
#include <setjmp.h>

/* Non-local goto with saving of signal mask */
/* NOTE: Use them when longjmp from signal handlers! */

/* TODO: some SIGLONGJMP may be LONGJMP */

#if defined(SYMM) || defined(Solaris) || defined(_WIN32) || defined(_WIN64) || defined(EMSCRIPTEN)
#define SIGJMP_BUF jmp_buf
#define SIGSETJMP(Env) setjmp(Env)
#define SIGLONGJMP(Env, Val) longjmp(Env, Val)
#else
#define SIGJMP_BUF sigjmp_buf
#define SIGSETJMP(Env) sigsetjmp(Env, 1)
#define SIGLONGJMP(Env, Val) siglongjmp(Env, Val)
#endif

/* NOTE: Use sigprocmask() for sigblock() */ 
/* NOTE: Use sigsuspend() for sigpause() */ 

#if defined(WIN32_NATIVE)
#include <ciao/win32_native.h>
#define SIGNAL win32_signal
#elif defined(Solaris)
#define SIGNAL(SIG,HDL) {                       \
  struct sigaction act;                         \
  sigemptyset(&act.sa_mask);                    \
  act.sa_flags = 0 | SA_NODEFER;                \
  act.sa_handler = HDL;                         \
  sigaction(SIG,&act,(struct sigaction *)NULL); \
}
#else
#define SIGNAL signal
#endif


#if defined(WIN32_NATIVE)
/* Windows native: additional signal compatibility */
#if !defined(SIGALRM)
#define SIGALRM 14
#endif
#if !defined(SIGUSR1)
#define SIGUSR1 30
#endif
#if !defined(SIGUSR2)
#define SIGUSR2 31
#endif
#if !defined(SIGPIPE)
#define SIGPIPE 13
#endif
#if !defined(SIGBUS)
#define SIGBUS  7
#endif
#if !defined(SIGQUIT)
#define SIGQUIT 3
#endif
#if !defined(SIGHUP)
#define SIGHUP  1
#endif
#endif /* WIN32_NATIVE */

#endif /* _CIAO_OS_SIGNAL_H */
