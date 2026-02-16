/*
 *  system_win32.c
 *
 *  Windows-native system operations for Ciao Prolog engine.
 *
 *  Additional Win32 API replacements for functions in system.c:
 *  - Shared memory (shm_open -> CreateFileMapping)
 *  - Temp files (mkstemp replacement)
 *  - Symlinks (readlink replacement)
 *  - User/group info stubs
 *
 *  Applied as a supplementary compilation unit.
 */

#if defined(_WIN32) || defined(_WIN64)

#include <windows.h>
#include <shlwapi.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <direct.h>

/* ------------------------------------------------------------------ */
/* Shared memory: shm_open / shm_unlink -> Windows File Mapping       */
/* ------------------------------------------------------------------ */

typedef struct {
    HANDLE hMapping;
    void  *base;
    size_t size;
    char   name[256];
} win32_shm_t;

#define MAX_SHM_HANDLES 64
static win32_shm_t g_shm_table[MAX_SHM_HANDLES];
static int g_shm_count = 0;

/*
 * Create or open a shared memory region (replacement for shm_open + mmap).
 */
void *win32_shm_open(const char *name, size_t size, int create) {
    if (g_shm_count >= MAX_SHM_HANDLES) {
        errno = ENOMEM;
        return NULL;
    }

    /* Build Windows shared memory name */
    char fullname[300];
    snprintf(fullname, sizeof(fullname), "Local\\CiaoProlog_%s", name);

    /* Convert to wide string */
    wchar_t wname[300];
    MultiByteToWideChar(CP_UTF8, 0, fullname, -1, wname, 300);

    HANDLE hMapping;
    
    if (create) {
        DWORD hi = (DWORD)((unsigned __int64)size >> 32);
        DWORD lo = (DWORD)(size & 0xFFFFFFFF);
        hMapping = CreateFileMappingW(
            INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
            hi, lo, wname);
    } else {
        hMapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname);
    }

    if (!hMapping) {
        errno = EACCES;
        return NULL;
    }

    void *ptr = MapViewOfFile(hMapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!ptr) {
        CloseHandle(hMapping);
        errno = ENOMEM;
        return NULL;
    }

    /* Store in table */
    win32_shm_t *entry = &g_shm_table[g_shm_count++];
    entry->hMapping = hMapping;
    entry->base = ptr;
    entry->size = size;
    strncpy(entry->name, name, sizeof(entry->name) - 1);

    return ptr;
}

int win32_shm_close(const char *name) {
    int i;
    for (i = 0; i < g_shm_count; i++) {
        if (strcmp(g_shm_table[i].name, name) == 0) {
            UnmapViewOfFile(g_shm_table[i].base);
            CloseHandle(g_shm_table[i].hMapping);
            /* Shift remaining entries */
            if (i < g_shm_count - 1) {
                memmove(&g_shm_table[i], &g_shm_table[i+1], 
                        (g_shm_count - i - 1) * sizeof(win32_shm_t));
            }
            g_shm_count--;
            return 0;
        }
    }
    errno = ENOENT;
    return -1;
}

/* ------------------------------------------------------------------ */
/* mkstemp replacement                                                */
/* ------------------------------------------------------------------ */

int win32_mkstemp(char *tmpl) {
    /* tmpl must end with XXXXXX */
    size_t len = strlen(tmpl);
    if (len < 6) {
        errno = EINVAL;
        return -1;
    }

    char *suffix = tmpl + len - 6;
    if (strcmp(suffix, "XXXXXX") != 0) {
        errno = EINVAL;
        return -1;
    }

    /* Generate unique temp filename */
    for (int attempts = 0; attempts < 100; attempts++) {
        LARGE_INTEGER ticks;
        QueryPerformanceCounter(&ticks);
        unsigned int seed = (unsigned int)(ticks.LowPart ^ GetCurrentThreadId() ^ attempts);
        
        for (int i = 0; i < 6; i++) {
            seed = seed * 1103515245 + 12345;
            int c = (seed >> 16) % 36;
            suffix[i] = (c < 10) ? ('0' + c) : ('a' + c - 10);
        }

        int fd = _open(tmpl, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY, 
                       _S_IREAD | _S_IWRITE);
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }

    errno = EEXIST;
    return -1;
}

/* ------------------------------------------------------------------ */
/* readlink stub (Windows doesn't have standard symlinks the same way) */
/* ------------------------------------------------------------------ */

int win32_readlink(const char *path, char *buf, size_t bufsize) {
    /* Windows reparse points / junction points / symlinks */
    wchar_t wpath[MAX_PATH];
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, MAX_PATH);

    HANDLE hFile = CreateFileW(wpath, 
        GENERIC_READ, FILE_SHARE_READ, NULL,
        OPEN_EXISTING, 
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
        NULL);

    if (hFile == INVALID_HANDLE_VALUE) {
        errno = ENOENT;
        return -1;
    }

    /* Use GetFinalPathNameByHandle for symlink resolution */
    wchar_t wresult[MAX_PATH];
    DWORD len = GetFinalPathNameByHandleW(hFile, wresult, MAX_PATH, 
                                           FILE_NAME_NORMALIZED);
    CloseHandle(hFile);

    if (len == 0 || len >= MAX_PATH) {
        errno = EINVAL;
        return -1;
    }

    /* Convert back to UTF-8 */
    /* Strip \\?\ prefix if present */
    wchar_t *start = wresult;
    if (wcsncmp(start, L"\\\\?\\", 4) == 0) {
        start += 4;
        len -= 4;
    }

    int result = WideCharToMultiByte(CP_UTF8, 0, start, -1, buf, (int)bufsize, NULL, NULL);
    if (result == 0) {
        errno = EINVAL;
        return -1;
    }

    return result - 1; /* Don't count null terminator */
}

/* ------------------------------------------------------------------ */
/* User/Group stubs (no UNIX users on Windows)                        */
/* ------------------------------------------------------------------ */

int win32_getuid(void) { return 0; }
int win32_getgid(void) { return 0; }
int win32_geteuid(void) { return 0; }
int win32_getegid(void) { return 0; }

const char *win32_get_username(void) {
    static char username[256] = "";
    if (username[0] == '\0') {
        DWORD size = sizeof(username);
        if (!GetUserNameA(username, &size)) {
            strcpy(username, "unknown");
        }
    }
    return username;
}

const char *win32_get_homedir(void) {
    static char homedir[MAX_PATH] = "";
    if (homedir[0] == '\0') {
        const char *userprofile = getenv("USERPROFILE");
        if (userprofile) {
            strncpy(homedir, userprofile, MAX_PATH - 1);
        } else {
            const char *homedrive = getenv("HOMEDRIVE");
            const char *homepath = getenv("HOMEPATH");
            if (homedrive && homepath) {
                snprintf(homedir, MAX_PATH, "%s%s", homedrive, homepath);
            } else {
                strcpy(homedir, "C:\\Users\\Default");
            }
        }
    }
    return homedir;
}

/* ------------------------------------------------------------------ */
/* getpwnam / getpwuid stubs                                          */
/* ------------------------------------------------------------------ */

struct win32_passwd {
    char *pw_name;
    char *pw_dir;
    char *pw_shell;
    int   pw_uid;
    int   pw_gid;
};

static struct win32_passwd g_passwd;

struct win32_passwd *win32_getpwnam(const char *name) {
    static char s_name[256];
    static char s_dir[MAX_PATH];
    
    strncpy(s_name, name, sizeof(s_name) - 1);
    strncpy(s_dir, win32_get_homedir(), sizeof(s_dir) - 1);
    
    g_passwd.pw_name = s_name;
    g_passwd.pw_dir = s_dir;
    g_passwd.pw_shell = "cmd.exe";
    g_passwd.pw_uid = 0;
    g_passwd.pw_gid = 0;
    
    return &g_passwd;
}

struct win32_passwd *win32_getpwuid(int uid) {
    (void)uid;
    return win32_getpwnam(win32_get_username());
}

/* ------------------------------------------------------------------ */
/* Miscellaneous POSIX stubs                                          */
/* ------------------------------------------------------------------ */

/* Windows doesn't need sync() - it has FlushFileBuffers */
void win32_sync(void) {
    /* No-op on Windows. The OS handles disk caching. */
}

/* sleep() - Windows has Sleep() in milliseconds */
unsigned int win32_sleep(unsigned int seconds) {
    Sleep(seconds * 1000);
    return 0;
}

/* usleep() replacement */
int win32_usleep(unsigned int usec) {
    /* Windows minimum is 1ms via Sleep, use waitable timer for better accuracy */
    if (usec >= 1000) {
        Sleep(usec / 1000);
    } else if (usec > 0) {
        /* Spin-wait for sub-millisecond delays */
        LARGE_INTEGER freq, start, now;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        double target = (double)usec / 1000000.0;
        do {
            QueryPerformanceCounter(&now);
        } while ((double)(now.QuadPart - start.QuadPart) / freq.QuadPart < target);
    }
    return 0;
}

/* gettimeofday replacement */
int win32_gettimeofday(struct timeval *tv) {
    FILETIME ft;
    ULARGE_INTEGER ull;
    
    GetSystemTimeAsFileTime(&ft);
    ull.LowPart = ft.dwLowDateTime;
    ull.HighPart = ft.dwHighDateTime;
    
    /* FILETIME is 100-nanosecond intervals since January 1, 1601 */
    /* Unix epoch is January 1, 1970 */
    ull.QuadPart -= 116444736000000000ULL;
    
    tv->tv_sec = (long)(ull.QuadPart / 10000000);
    tv->tv_usec = (long)((ull.QuadPart % 10000000) / 10);
    
    return 0;
}

#endif /* _WIN32 || _WIN64 */
