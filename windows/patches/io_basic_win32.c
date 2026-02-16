/*
 *  io_basic_win32.c
 *
 *  Windows-native file path normalization and I/O patches
 *  for Ciao Prolog engine (io_basic.c).
 *
 *  This file provides Win32 API replacements for POSIX I/O
 *  operations that are not available in native Windows builds.
 *
 *  Applied as a supplementary compilation unit linked with the engine.
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
/* Path normalization: convert POSIX paths to Windows paths           */
/* ------------------------------------------------------------------ */

/*
 * Normalize a file path for Windows:
 *  - Replace '/' with '\\'
 *  - Handle drive letters (C:/ -> C:\)
 *  - Collapse consecutive separators
 *  - Remove trailing separator (unless root)
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

    /* Remove trailing separator unless it's root like C:\ */
    if (dst > win_path + 1 && *(dst - 1) == '\\') {
        if (!(dst == win_path + 3 && win_path[1] == ':')) {
            dst--;
        }
    }

    *dst = '\0';
    return 0;
}

/*
 * Convert a Windows path back to POSIX-style (for internal use):
 *  - Replace '\\' with '/'
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
/* select() replacement for stdin readiness check                     */
/* ------------------------------------------------------------------ */

/*
 * Check if stdin has data available (non-blocking).
 * Used by the engine REPL to check for input readiness.
 */
int win32_stdin_ready(void) {
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;

    if (hStdin == INVALID_HANDLE_VALUE) return 0;

    if (GetConsoleMode(hStdin, &mode)) {
        /* Console input */
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
        /* Pipe/file input - use PeekNamedPipe */
        DWORD bytesAvail = 0;
        if (PeekNamedPipe(hStdin, NULL, 0, NULL, &bytesAvail, NULL)) {
            return (bytesAvail > 0) ? 1 : 0;
        }
        /* For regular files, always ready */
        return 1;
    }
}

/* ------------------------------------------------------------------ */
/* Console I/O for REPL                                               */
/* ------------------------------------------------------------------ */

static HANDLE hConsoleIn = INVALID_HANDLE_VALUE;
static HANDLE hConsoleOut = INVALID_HANDLE_VALUE;
static DWORD  origConsoleMode = 0;

int win32_console_init(void) {
    hConsoleIn = GetStdHandle(STD_INPUT_HANDLE);
    hConsoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
    
    if (hConsoleIn == INVALID_HANDLE_VALUE || hConsoleOut == INVALID_HANDLE_VALUE) {
        return -1;
    }
    
    /* Save original console mode */
    GetConsoleMode(hConsoleIn, &origConsoleMode);
    
    /* Enable virtual terminal processing for ANSI escape codes */
    DWORD outMode = 0;
    if (GetConsoleMode(hConsoleOut, &outMode)) {
        outMode |= 0x0004; /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */
        SetConsoleMode(hConsoleOut, outMode);
    }
    
    /* Set console to UTF-8 */
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    
    return 0;
}

void win32_console_cleanup(void) {
    if (hConsoleIn != INVALID_HANDLE_VALUE) {
        SetConsoleMode(hConsoleIn, origConsoleMode);
    }
}

/* Write a Unicode string to the console */
int win32_console_write(const wchar_t *str, int len) {
    DWORD written;
    if (hConsoleOut == INVALID_HANDLE_VALUE) return -1;
    WriteConsoleW(hConsoleOut, str, len, &written, NULL);
    return (int)written;
}

/* Read a Unicode character from the console */
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
