/*
 *  signals_win32.c
 *
 *  POSIX signal emulation for the Windows native build.
 *
 *  Replaces UNIX signal delivery with Win32 equivalents:
 *    - Ctrl+C via SetConsoleCtrlHandler
 *    - alarm() via Win32 timer queue
 *    - External interrupt via named event "CiaoInterrupt_{PID}"
 *      (used by signal_ciao.exe / VS Code extension)
 *    - signal()/kill() wrappers
 *    - sigprocmask() stub (no-op)
 */

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Internal state                                                     */
/* ------------------------------------------------------------------ */

static HANDLE g_interrupt_event = NULL;      /* set on any interrupt   */
static HANDLE g_alarm_event = NULL;          /* set on alarm timer     */
static HANDLE g_alarm_timer = NULL;          /* active alarm timer     */
static HANDLE g_ext_interrupt_event = NULL;  /* named event for IPC    */
static HANDLE g_ext_interrupt_thread = NULL; /* watcher thread handle  */
static volatile LONG g_ext_interrupt_shutdown = 0;

typedef void (*signal_handler_t)(int);
static signal_handler_t g_sigint_handler = SIG_DFL;
static signal_handler_t g_sigalrm_handler = SIG_DFL;
static signal_handler_t g_sigusr1_handler = SIG_DFL;

/* ------------------------------------------------------------------ */
/* Ctrl+C handler                                                     */
/* ------------------------------------------------------------------ */

static BOOL WINAPI win32_ctrl_handler(DWORD dwCtrlType) {
    switch (dwCtrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
            if (g_sigint_handler != SIG_DFL && g_sigint_handler != SIG_IGN) {
                g_sigint_handler(SIGINT);
            } else if (g_sigint_handler == SIG_DFL) {
                return FALSE; /* let Windows terminate */
            }
            if (g_interrupt_event) SetEvent(g_interrupt_event);
            return TRUE;

        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (g_interrupt_event) SetEvent(g_interrupt_event);
            return FALSE;

        default:
            return FALSE;
    }
}

/* ------------------------------------------------------------------ */
/* External interrupt watcher thread                                  */
/*                                                                    */
/* Waits on named event "CiaoInterrupt_{PID}" so that external tools  */
/* (signal_ciao.exe, VS Code) can deliver SIGINT to the engine.       */
/* Polls every 500ms to check the shutdown flag.                      */
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
/* Init / cleanup                                                     */
/* ------------------------------------------------------------------ */

int win32_signals_init(void) {
    g_interrupt_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    g_alarm_event = CreateEventW(NULL, FALSE, FALSE, NULL);

    if (!g_interrupt_event || !g_alarm_event) {
        return -1;
    }

    SetConsoleCtrlHandler(win32_ctrl_handler, TRUE);

    /* Create named event for external interrupt (VS Code, signal_ciao.exe) */
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

    /* Shut down watcher thread */
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

/* ------------------------------------------------------------------ */
/* signal() replacement                                               */
/*                                                                    */
/* For signals the CRT doesn't handle (SIGALRM, SIGUSR1), we store   */
/* the handler ourselves. CRT-supported signals pass through.         */
/* ------------------------------------------------------------------ */

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
            return signal(sig, handler);
    }
}

/* ------------------------------------------------------------------ */
/* alarm() replacement via timer queue                                */
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

/* Schedule SIGALRM after `seconds`. Cancels any previous alarm. */
unsigned int win32_alarm(unsigned int seconds) {
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

    return 0;
}

/* ------------------------------------------------------------------ */
/* Blocking wait on interrupt event                                   */
/* ------------------------------------------------------------------ */

int win32_wait_interrupt(DWORD timeout_ms) {
    if (!g_interrupt_event) return -1;
    DWORD result = WaitForSingleObject(g_interrupt_event, timeout_ms);
    return (result == WAIT_OBJECT_0) ? 0 : -1;
}

/* ------------------------------------------------------------------ */
/* sigprocmask stub (no-op on Windows)                                */
/* ------------------------------------------------------------------ */

#if !defined(SIG_BLOCK)
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#endif

typedef unsigned long win32_sigset_t;

int win32_sigprocmask(int how, const win32_sigset_t *set, win32_sigset_t *oldset) {
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
/* kill() replacement                                                 */
/*                                                                    */
/* For self: dispatch via handler or raise(). For other processes:     */
/* sig == 0 checks existence, otherwise TerminateProcess.             */
/* ------------------------------------------------------------------ */

int win32_kill(int pid, int sig) {
    HANDLE hProcess;

    if (pid == 0 || pid == (int)GetCurrentProcessId()) {
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
