/*
 *  process_win32.c
 *
 *  Windows-native process creation for Ciao Prolog engine.
 *
 *  Replaces fork()/exec() with CreateProcessW() for native Windows.
 *  Provides pipe redirection for stdin/stdout/stderr.
 *
 *  Applied as a supplementary compilation unit.
 */

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <process.h>

/* ------------------------------------------------------------------ */
/* Structure for process creation with redirected I/O                 */
/* ------------------------------------------------------------------ */

typedef struct win32_process_info {
    DWORD pid;
    HANDLE hProcess;
    HANDLE hThread;
    int stdin_fd;   /* Parent's write end for child stdin */
    int stdout_fd;  /* Parent's read end for child stdout */
    int stderr_fd;  /* Parent's read end for child stderr */
} win32_process_info_t;

/* ------------------------------------------------------------------ */
/* Helper: Build command line string from argv array                  */
/* ------------------------------------------------------------------ */

static wchar_t *build_command_line(const char **argv) {
    /* Calculate required buffer size */
    size_t total = 0;
    int i;
    for (i = 0; argv[i] != NULL; i++) {
        total += strlen(argv[i]) + 3; /* quotes + space */
    }
    total += 1; /* null terminator */

    wchar_t *cmdline = (wchar_t *)malloc(total * sizeof(wchar_t));
    if (!cmdline) return NULL;

    wchar_t *p = cmdline;
    for (i = 0; argv[i] != NULL; i++) {
        if (i > 0) *p++ = L' ';

        /* Quote argument if it contains spaces */
        int needs_quotes = (strchr(argv[i], ' ') != NULL || 
                           strchr(argv[i], '\t') != NULL ||
                           argv[i][0] == '\0');

        if (needs_quotes) *p++ = L'"';

        /* Convert to wide char */
        int len = MultiByteToWideChar(CP_UTF8, 0, argv[i], -1, p, (int)(total - (p - cmdline)));
        if (len > 0) p += len - 1; /* -1 to skip null terminator */

        if (needs_quotes) *p++ = L'"';
    }
    *p = L'\0';

    return cmdline;
}

/* ------------------------------------------------------------------ */
/* Helper: Create an inheritable pipe                                 */
/* ------------------------------------------------------------------ */

static int create_inheritable_pipe(HANDLE *read_handle, HANDLE *write_handle, 
                                    BOOL inherit_read) {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(read_handle, write_handle, &sa, 0)) {
        return -1;
    }

    /* Make the non-inherited end non-inheritable */
    if (inherit_read) {
        SetHandleInformation(*write_handle, HANDLE_FLAG_INHERIT, 0);
    } else {
        SetHandleInformation(*read_handle, HANDLE_FLAG_INHERIT, 0);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Main process creation function                                     */
/* ------------------------------------------------------------------ */

/*
 * Create a child process with optional I/O redirection.
 *
 * @param cmd       Path to the executable (UTF-8)
 * @param argv      NULL-terminated argument array (UTF-8)
 * @param cwd       Working directory (UTF-8, or NULL for inherit)
 * @param env       Environment block (NULL for inherit)
 * @param redirect_stdin  If non-zero, redirect child's stdin
 * @param redirect_stdout If non-zero, redirect child's stdout
 * @param redirect_stderr If non-zero, redirect child's stderr
 * @param info      Output: process info and pipe file descriptors
 *
 * @return 0 on success, -1 on error (sets errno)
 */
int win32_create_process(
    const char *cmd,
    const char **argv,
    const char *cwd,
    const char *env,
    int redirect_stdin,
    int redirect_stdout,
    int redirect_stderr,
    win32_process_info_t *info)
{
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    HANDLE hStdinRead = NULL, hStdinWrite = NULL;
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;
    wchar_t *cmdline = NULL;
    wchar_t wcwd[MAX_PATH];
    BOOL success;

    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    memset(info, 0, sizeof(*info));
    info->stdin_fd = -1;
    info->stdout_fd = -1;
    info->stderr_fd = -1;

    /* Create pipes for I/O redirection */
    if (redirect_stdin) {
        if (create_inheritable_pipe(&hStdinRead, &hStdinWrite, TRUE) < 0) {
            goto error;
        }
    }
    if (redirect_stdout) {
        if (create_inheritable_pipe(&hStdoutRead, &hStdoutWrite, FALSE) < 0) {
            goto error;
        }
    }
    if (redirect_stderr) {
        if (create_inheritable_pipe(&hStderrRead, &hStderrWrite, FALSE) < 0) {
            goto error;
        }
    }

    /* Set up STARTUPINFO with redirected handles */
    if (redirect_stdin || redirect_stdout || redirect_stderr) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdInput = redirect_stdin ? hStdinRead : GetStdHandle(STD_INPUT_HANDLE);
        si.hStdOutput = redirect_stdout ? hStdoutWrite : GetStdHandle(STD_OUTPUT_HANDLE);
        si.hStdError = redirect_stderr ? hStderrWrite : GetStdHandle(STD_ERROR_HANDLE);
    }

    /* Build command line */
    cmdline = build_command_line(argv);
    if (!cmdline) {
        errno = ENOMEM;
        goto error;
    }

    /* Convert working directory */
    if (cwd) {
        MultiByteToWideChar(CP_UTF8, 0, cwd, -1, wcwd, MAX_PATH);
    }

    /* Create the process */
    success = CreateProcessW(
        NULL,           /* lpApplicationName - use cmdline */
        cmdline,        /* lpCommandLine */
        NULL,           /* lpProcessAttributes */
        NULL,           /* lpThreadAttributes */
        TRUE,           /* bInheritHandles */
        CREATE_NO_WINDOW, /* dwCreationFlags */
        (LPVOID)env,    /* lpEnvironment */
        cwd ? wcwd : NULL, /* lpCurrentDirectory */
        &si,            /* lpStartupInfo */
        &pi             /* lpProcessInformation */
    );

    free(cmdline);

    if (!success) {
        DWORD err = GetLastError();
        switch (err) {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                errno = ENOENT;
                break;
            case ERROR_ACCESS_DENIED:
                errno = EACCES;
                break;
            default:
                errno = ECHILD;
                break;
        }
        goto error;
    }

    /* Close child-side pipe handles (parent doesn't need them) */
    if (hStdinRead) CloseHandle(hStdinRead);
    if (hStdoutWrite) CloseHandle(hStdoutWrite);
    if (hStderrWrite) CloseHandle(hStderrWrite);

    /* Convert Windows HANDLEs to C file descriptors for parent */
    if (redirect_stdin) {
        info->stdin_fd = _open_osfhandle((intptr_t)hStdinWrite, _O_WRONLY);
    }
    if (redirect_stdout) {
        info->stdout_fd = _open_osfhandle((intptr_t)hStdoutRead, _O_RDONLY);
    }
    if (redirect_stderr) {
        info->stderr_fd = _open_osfhandle((intptr_t)hStderrRead, _O_RDONLY);
    }

    info->pid = pi.dwProcessId;
    info->hProcess = pi.hProcess;
    info->hThread = pi.hThread;

    return 0;

error:
    if (hStdinRead) CloseHandle(hStdinRead);
    if (hStdinWrite) CloseHandle(hStdinWrite);
    if (hStdoutRead) CloseHandle(hStdoutRead);
    if (hStdoutWrite) CloseHandle(hStdoutWrite);
    if (hStderrRead) CloseHandle(hStderrRead);
    if (hStderrWrite) CloseHandle(hStderrWrite);
    free(cmdline);
    return -1;
}

/* ------------------------------------------------------------------ */
/* Wait for process termination                                       */
/* ------------------------------------------------------------------ */

int win32_wait_process(win32_process_info_t *info, int *exit_code, DWORD timeout_ms) {
    DWORD result = WaitForSingleObject(info->hProcess, timeout_ms);
    
    if (result == WAIT_OBJECT_0) {
        DWORD code;
        GetExitCodeProcess(info->hProcess, &code);
        if (exit_code) *exit_code = (int)code;
        return 0;
    } else if (result == WAIT_TIMEOUT) {
        errno = EAGAIN;
        return 1; /* Still running */
    } else {
        errno = ECHILD;
        return -1;
    }
}

/* ------------------------------------------------------------------ */
/* Cleanup process handles                                            */
/* ------------------------------------------------------------------ */

void win32_close_process(win32_process_info_t *info) {
    if (info->stdin_fd >= 0) _close(info->stdin_fd);
    if (info->stdout_fd >= 0) _close(info->stdout_fd);
    if (info->stderr_fd >= 0) _close(info->stderr_fd);
    if (info->hProcess) CloseHandle(info->hProcess);
    if (info->hThread) CloseHandle(info->hThread);
    memset(info, 0, sizeof(*info));
    info->stdin_fd = info->stdout_fd = info->stderr_fd = -1;
}

/* ------------------------------------------------------------------ */
/* fork() stub - returns error on Windows                             */
/* ------------------------------------------------------------------ */

/*
 * fork() is not available on Windows. This stub returns -1 with ENOSYS.
 * Code that needs fork() semantics should use win32_create_process().
 */
int win32_fork_stub(void) {
    fprintf(stderr, "ERROR: fork() is not supported on native Windows.\n"
                    "Use process creation APIs instead.\n");
    errno = ENOSYS;
    return -1;
}

/* ------------------------------------------------------------------ */
/* setsid() stub                                                      */
/* ------------------------------------------------------------------ */

int win32_setsid_stub(void) {
    /* No concept of session ID on Windows. No-op. */
    return (int)GetCurrentProcessId();
}

/* ------------------------------------------------------------------ */
/* exec*() replacements using CreateProcess                           */
/* ------------------------------------------------------------------ */

int win32_execvp(const char *file, const char **argv) {
    win32_process_info_t info;
    int exit_code = 127;

    if (win32_create_process(file, argv, NULL, NULL, 0, 0, 0, &info) != 0) {
        return -1;
    }

    win32_wait_process(&info, &exit_code, INFINITE);
    win32_close_process(&info);

    /* exec should not return - but on Windows we emulate by exiting with child's code */
    exit(exit_code);
    return -1; /* Never reached */
}

#endif /* _WIN32 || _WIN64 */
