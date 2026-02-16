/*
 *  io_basic_win32.c
 *
 *  Path normalization and console I/O for the Windows native build.
 *  Provides POSIX/Windows path conversion, non-blocking stdin check,
 *  and UTF-8 console initialization for the interactive REPL.
 */

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* ------------------------------------------------------------------ */
/* Path normalization                                                 */
/* ------------------------------------------------------------------ */

/*
 * Convert a POSIX-style path to Windows native format:
 *   - Replace '/' with '\\'
 *   - Handle MSYS2 drive letters (/c/ -> C:\)
 *   - Collapse consecutive separators
 *   - Strip trailing separator (unless root like C:\)
 *
 * Returns 0 on success, -1 on error.
 */
int win32_normalize_path(const char *posix_path, char *win_path, size_t bufsize) {
    const char *src = posix_path;
    char *dst = win_path;
    char *end = win_path + bufsize - 1;
    int prev_was_sep = 0;

    if (!posix_path || !win_path || bufsize == 0) {
        return -1;
    }

    /* Handle drive letter prefix: /c/ -> C:\ */
    if (src[0] == '/' && ((src[1] >= 'a' && src[1] <= 'z') || 
        (src[1] >= 'A' && src[1] <= 'Z')) && (src[2] == '/' || src[2] == '\0')) {
        if (dst + 3 > end) return -1;
        *dst++ = (char)(src[1] & ~0x20); /* uppercase */
        *dst++ = ':';
        *dst++ = '\\';
        src += (src[2] == '/') ? 3 : 2;
        prev_was_sep = 1;
    }

    while (*src && dst < end) {
        if (*src == '/' || *src == '\\') {
            if (!prev_was_sep) {
                *dst++ = '\\';
                prev_was_sep = 1;
            }
            src++;
        } else {
            *dst++ = *src++;
            prev_was_sep = 0;
        }
    }

    /* Remove trailing separator unless it's a drive root like C:\ */
    if (dst > win_path + 1 && *(dst - 1) == '\\') {
        if (!(dst == win_path + 3 && win_path[1] == ':')) {
            dst--;
        }
    }

    *dst = '\0';
    return 0;
}

/*
 * Convert a Windows path to POSIX-style (replaces '\\' with '/').
 * Used when the engine receives paths from Win32 APIs and needs to
 * store them in the internal format.
 */
int win32_to_posix_path(const char *win_path, char *posix_path, size_t bufsize) {
    const char *src = win_path;
    char *dst = posix_path;
    char *end = posix_path + bufsize - 1;

    if (!win_path || !posix_path || bufsize == 0) {
        return -1;
    }

    while (*src && dst < end) {
        *dst++ = (*src == '\\') ? '/' : *src;
        src++;
    }
    *dst = '\0';
    return 0;
}

/* ------------------------------------------------------------------ */
/* Non-blocking stdin check (replaces select(2) on stdin)             */
/* ------------------------------------------------------------------ */

/*
 * Check if stdin has data available without blocking.
 * Returns 1 if ready, 0 otherwise.
 *
 * For console input, we peek the input queue and skip non-key events
 * (mouse, resize, focus). For pipes, we use PeekNamedPipe.
 * For regular files, always ready.
 */
int win32_stdin_ready(void) {
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (hStdin == INVALID_HANDLE_VALUE) return 0;

    if (GetConsoleMode(hStdin, &mode)) {
        /* Console: scan for actual key-down events with characters */
        DWORD numEvents = 0;
        if (!GetNumberOfConsoleInputEvents(hStdin, &numEvents)) return 0;
        
        if (numEvents > 0) {
            INPUT_RECORD *records = (INPUT_RECORD *)malloc(numEvents * sizeof(INPUT_RECORD));
            DWORD numRead = 0;
            if (records && PeekConsoleInput(hStdin, records, numEvents, &numRead)) {
                DWORD i;
                for (i = 0; i < numRead; i++) {
                    if (records[i].EventType == KEY_EVENT && 
                        records[i].Event.KeyEvent.bKeyDown &&
                        records[i].Event.KeyEvent.uChar.AsciiChar != 0) {
                        free(records);
                        return 1;
                    }
                }
            }
            free(records);
        }
        return 0;
    } else {
        /* Pipe: check with PeekNamedPipe; file: always ready */
        DWORD bytesAvail = 0;
        if (PeekNamedPipe(hStdin, NULL, 0, NULL, &bytesAvail, NULL)) {
            return (bytesAvail > 0) ? 1 : 0;
        }
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Console initialization                                             */
/* ------------------------------------------------------------------ */

static HANDLE hConsoleIn = INVALID_HANDLE_VALUE;
static HANDLE hConsoleOut = INVALID_HANDLE_VALUE;
static DWORD  origConsoleMode = 0;

/*
 * Configure the console for the Ciao REPL:
 *   - Enable ANSI escape codes (ENABLE_VIRTUAL_TERMINAL_PROCESSING)
 *   - Set codepage to UTF-8 (65001)
 */
int win32_console_init(void) {
    hConsoleIn = GetStdHandle(STD_INPUT_HANDLE);
    hConsoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
    
    if (hConsoleIn == INVALID_HANDLE_VALUE || hConsoleOut == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    GetConsoleMode(hConsoleIn, &origConsoleMode);
    
    /* ENABLE_VIRTUAL_TERMINAL_PROCESSING (0x0004) - Win10 1511+ */
    DWORD outMode = 0;
    if (GetConsoleMode(hConsoleOut, &outMode)) {
        outMode |= 0x0004;
        SetConsoleMode(hConsoleOut, outMode);
    }
    
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    
    return 0;
}

/* Restore original console mode on exit. */
void win32_console_cleanup(void) {
    if (hConsoleIn != INVALID_HANDLE_VALUE) {
        SetConsoleMode(hConsoleIn, origConsoleMode);
    }
}

/* Write a UTF-16 string to the console. Returns chars written or -1. */
int win32_console_write(const wchar_t *str, int len) {
    DWORD written;
    if (hConsoleOut == INVALID_HANDLE_VALUE) return -1;
    WriteConsoleW(hConsoleOut, str, len, &written, NULL);
    return (int)written;
}

/* Read a single character from the console (blocks until key-down). */
int win32_console_read(wchar_t *ch) {
    DWORD read;
    if (hConsoleIn == INVALID_HANDLE_VALUE) return -1;
    INPUT_RECORD rec;
    
    while (1) {
        if (!ReadConsoleInputW(hConsoleIn, &rec, 1, &read)) return -1;
        if (read == 0) return -1;
        if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
            *ch = rec.Event.KeyEvent.uChar.UnicodeChar;
            if (*ch != 0) return 1;
        }
    }
}

#endif /* _WIN32 || _WIN64 */
