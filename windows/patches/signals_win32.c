/*
 *  signals_win32.c
 *
 *  Windows-native signal handling replacements for Ciao Prolog engine.
 *
 *  Replaces UNIX signal mechanisms with Windows equivalents:
 *  - Ctrl+C via SetConsoleCtrlHandler
 *  - Timer events via Windows timers
 *  - Structured exception handling for SIGSEGV/SIGFPE equivalents
 *
 *  Applied as a supplementary compilation unit.
 */

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Windows event objects replacing UNIX signals                       */
/* ------------------------------------------------------------------ */

static HANDLE g_interrupt_event = NULL;
static HANDLE g_alarm_event = NULL;
static HANDLE g_alarm_timer = NULL;
static HANDLE g_ext_interrupt_event = NULL;  /* named event for external interrupt */
static HANDLE g_ext_interrupt_thread = NULL; /* watcher thread */
static volatile LONG g_ext_interrupt_shutdown = 0;

typedef void (*signal_handler_t)(int);
static signal_handler_t g_sigint_handler = SIG_DFL;
static signal_handler_t g_sigalrm_handler = SIG_DFL;
static signal_handler_t g_sigusr1_handler = SIG_DFL;

/* ------------------------------------------------------------------ */
/* Ctrl+C Handler                                                     */
/* ------------------------------------------------------------------ */

static BOOL WINAPI win32_ctrl_handler(DWORD dwCtrlType) {
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            if (g_sigint_handler != SIG_DFL && g_sigint_handler != SIG_IGN) {
                g_sigint_handler(SIGINT);
            } else if (g_sigint_handler == SIG_DFL) {
                /* Default: terminate */
                return FALSE; /* Let Windows handle it */
            }
            /* SIG_IGN: ignore */
            if (g_interrupt_event) SetEvent(g_interrupt_event);
            return TRUE;

        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            /* Graceful shutdown */
            if (g_interrupt_event) SetEvent(g_interrupt_event);
            return FALSE;

        default:
            return FALSE;
    }
}

/* ------------------------------------------------------------------ */
/* External interrupt watcher thread                                  */
/* Waits on a named event "CiaoInterrupt_{PID}" so that external     */
/* tools (e.g. VS Code extension) can deliver SIGINT to the engine.  */
/* ------------------------------------------------------------------ */

static DWORD WINAPI ext_interrupt_watcher(LPVOID param) {
    (void)param;
    while (!InterlockedCompareExchange(&g_ext_interrupt_shutdown, 0, 0)) {
        DWORD r = WaitForSingleObject(g_ext_interrupt_event, 500);
        if (r == WAIT_OBJECT_0) {
            if (g_sigint_handler != SIG_DFL && g_sigint_handler != SIG_IGN) {
                g_sigint_handler(SIGINT);
            }
            if (g_interrupt_event) SetEvent(g_interrupt_event);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Signal emulation layer                                             */
/* ------------------------------------------------------------------ */

int win32_signals_init(void) {
    g_interrupt_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_alarm_event = CreateEventW(NULL, FALSE, FALSE, NULL);

    if (!g_interrupt_event || !g_alarm_event) {
        return -1;
    }

    SetConsoleCtrlHandler(win32_ctrl_handler, TRUE);

    /* Create named event for external interrupt signaling */
    {
        char evname[128];
        snprintf(evname, sizeof(evname), "CiaoInterrupt_%lu",
                 (unsigned long)GetCurrentProcessId());
        g_ext_interrupt_event = CreateEventA(NULL, FALSE, FALSE, evname);
        if (g_ext_interrupt_event) {
            InterlockedExchange(&g_ext_interrupt_shutdown, 0);
            g_ext_interrupt_thread = CreateThread(
                NULL, 0, ext_interrupt_watcher, NULL, 0, NULL);
        }
    }

    return 0;
}

void win32_signals_cleanup(void) {
    SetConsoleCtrlHandler(win32_ctrl_handler, FALSE);

    /* Shut down external interrupt watcher */
    if (g_ext_interrupt_thread) {
        InterlockedExchange(&g_ext_interrupt_shutdown, 1);
        if (g_ext_interrupt_event) SetEvent(g_ext_interrupt_event);
        WaitForSingleObject(g_ext_interrupt_thread, 2000);
        CloseHandle(g_ext_interrupt_thread);
        g_ext_interrupt_thread = NULL;
    }
    if (g_ext_interrupt_event) {
        CloseHandle(g_ext_interrupt_event);
        g_ext_interrupt_event = NULL;
    }
    
    if (g_alarm_timer) {
        DeleteTimerQueueTimer(NULL, g_alarm_timer, INVALID_HANDLE_VALUE);
        g_alarm_timer = NULL;
    }
    if (g_interrupt_event) {
        CloseHandle(g_interrupt_event);
        g_interrupt_event = NULL;
    }
    if (g_alarm_event) {
        CloseHandle(g_alarm_event);
        g_alarm_event = NULL;
    }
}

/*
 * win32_signal: Drop-in replacement for signal() for signals
 * not supported by the MSVC/MinGW CRT.
 */
signal_handler_t win32_signal(int sig, signal_handler_t handler) {
    signal_handler_t prev;

    switch (sig) {
        case SIGINT:
            prev = g_sigint_handler;
            g_sigint_handler = handler;
            return prev;

#if !defined(SIGALRM)
#define SIGALRM 14
#endif
        case SIGALRM:
            prev = g_sigalrm_handler;
            g_sigalrm_handler = handler;
            return prev;

#if !defined(SIGUSR1)
#define SIGUSR1 30
#endif
        case SIGUSR1:
            prev = g_sigusr1_handler;
            g_sigusr1_handler = handler;
            return prev;

        default:
            /* For signals supported by CRT (SIGFPE, SIGSEGV, etc.),
               fall through to the standard signal() */
            return signal(sig, handler);
    }
}

/* ------------------------------------------------------------------ */
/* alarm() replacement using Windows timer queue                      */
/* ------------------------------------------------------------------ */

static VOID CALLBACK alarm_timer_callback(PVOID lpParam, BOOLEAN TimerOrWaitFired) {
    (void)lpParam;
    (void)TimerOrWaitFired;
    
    if (g_sigalrm_handler != SIG_DFL && g_sigalrm_handler != SIG_IGN) {
        g_sigalrm_handler(SIGALRM);
    }
    if (g_alarm_event) {
        SetEvent(g_alarm_event);
    }
}

unsigned int win32_alarm(unsigned int seconds) {
    /* Cancel previous alarm */
    if (g_alarm_timer) {
        DeleteTimerQueueTimer(NULL, g_alarm_timer, INVALID_HANDLE_VALUE);
        g_alarm_timer = NULL;
    }

    if (seconds == 0) return 0;

    DWORD ms = seconds * 1000;
    if (!CreateTimerQueueTimer(&g_alarm_timer, NULL, 
            alarm_timer_callback, NULL, ms, 0, 
            WT_EXECUTEONLYONCE)) {
        return 0;
    }

    return 0; /* Previous alarm time not tracked */
}

/* ------------------------------------------------------------------ */
/* Wait for interrupt event (blocking)                                */
/* ------------------------------------------------------------------ */

int win32_wait_interrupt(DWORD timeout_ms) {
    if (!g_interrupt_event) return -1;
    DWORD result = WaitForSingleObject(g_interrupt_event, timeout_ms);
    return (result == WAIT_OBJECT_0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* sigprocmask / sigaction stubs                                      */
/* ------------------------------------------------------------------ */

/* Minimal sigset_t operations for compatibility */
#if !defined(SIG_BLOCK)
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#endif

typedef unsigned long win32_sigset_t;

int win32_sigprocmask(int how, const win32_sigset_t *set, win32_sigset_t *oldset) {
    /* Windows doesn't have signal masks. This is a no-op stub. */
    static win32_sigset_t current_mask = 0;
    
    if (oldset) *oldset = current_mask;
    
    if (set) {
        switch (how) {
            case SIG_BLOCK:   current_mask |= *set; break;
            case SIG_UNBLOCK: current_mask &= ~(*set); break;
            case SIG_SETMASK: current_mask = *set; break;
        }
    }
    
    return 0;
}

/* ------------------------------------------------------------------ */
/* kill() enhancement for Windows                                     */
/* ------------------------------------------------------------------ */

int win32_kill(int pid, int sig) {
    HANDLE hProcess;

    if (pid == 0 || pid == (int)GetCurrentProcessId()) {
        /* Send signal to self */
        if (sig == SIGINT) {
            if (g_sigint_handler && g_sigint_handler != SIG_DFL && g_sigint_handler != SIG_IGN) {
                g_sigint_handler(sig);
            }
            return 0;
        }
        return raise(sig);
    }

    if (sig == 0) {
        /* Check if process exists */
        hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, (DWORD)pid);
        if (hProcess) {
            CloseHandle(hProcess);
            return 0;
        }
        return -1;
    }

    /* For other processes, the only option is TerminateProcess */
    hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid);
    if (!hProcess) {
        errno = ESRCH;
        return -1;
    }

    if (!TerminateProcess(hProcess, (UINT)sig)) {
        CloseHandle(hProcess);
        errno = EPERM;
        return -1;
    }

    CloseHandle(hProcess);
    return 0;
}

#endif /* _WIN32 || _WIN64 */
