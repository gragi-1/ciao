# Ciao Prolog - Windows Native Port

This directory contains all files needed to build and distribute Ciao Prolog
as a native Windows executable (no WSL, MSYS2, or Cygwin dependencies).

## Structure

```
windows/
├── patches/                    # Win32 API implementations (compiled as extra .c units)
│   ├── win32_native.h          # Master compat header (included by eng.h)
│   ├── io_basic_win32.c        # File path normalization, console I/O
│   ├── signals_win32.c         # Signal handling via Windows events
│   ├── process_win32.c         # CreateProcessW wrapper
│   └── system_win32.c          # OS detection, environment, temp files
├── build_scripts/
│   ├── bootstrap_windows.bat   # Main build script (compiles engine + toplevels)
│   └── validate_install.ps1    # Post-install validation / CI test
├── signal_ciao.c               # Helper for VS Code extension interrupt support
├── install.bat                 # Double-click installer launcher
├── install.ps1                 # PowerShell installer (copies files, sets PATH,
│                               #   generates bundle registry, patches VS Code extension)
├── package.ps1                 # Creates distributable CiaoProlog-Windows-x64.zip
└── README.md
```

## Building

### Prerequisites
- MinGW-w64 (`x86_64-w64-mingw32-gcc` in PATH)

### Quick Build
```cmd
cd windows\build_scripts
bootstrap_windows.bat
```

This produces a self-contained distribution in `dist/windows-static/` with:
- `ciao.exe` — statically linked engine (no DLL dependencies)
- `ciaosh.sta`, `ciao_builder.sta`, `lpdoc.sta` — saved states
- `.bat` wrappers for `ciaosh` and `ciao`
- All required Prolog libraries

### Packaging
```powershell
powershell -ExecutionPolicy Bypass -File windows\package.ps1
```
Creates `dist/CiaoProlog-Windows-x64.zip` (~5 MB).

## Installation (from zip)

1. Extract the zip anywhere
2. Double-click `install.bat` (or run `install.ps1` in PowerShell)
3. Open a **new** terminal and type `ciaosh`

No admin rights required. Installs to `%LOCALAPPDATA%\CiaoProlog`.

## Validation
```powershell
powershell -ExecutionPolicy Bypass -File windows\build_scripts\validate_install.ps1
```

## Architecture

The Windows port works by:
1. **Conditional compilation** — Engine source files (`core/engine/*.c`) contain
   `#if defined(WIN32_NATIVE)` blocks that call Win32 replacement functions
2. **Supplementary compilation units** — `windows/patches/*.c` provide the Win32
   implementations, compiled alongside the engine
3. **Static linking** — Everything links statically against MinGW CRT, producing
   a single `ciao.exe` with zero runtime dependencies

## Notes

- File paths in `ciaosh` must use forward slashes (`/`) or escaped backslashes
  (`\\`). This is standard ISO Prolog behavior (backslash is an escape character
  in quoted atoms).
- The VS Code extension is automatically patched during installation to work
  with the native Windows build.
