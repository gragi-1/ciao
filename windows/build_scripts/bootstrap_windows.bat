@echo off
REM =========================================================================
REM  bootstrap_windows.bat
REM  Build Ciao Prolog natively on Windows using MinGW-w64
REM  No MSYS2/Cygwin/WSL required at runtime
REM
REM  This script replicates the build_car.sh workflow using CMD:
REM   1. Create cardir layout (include/ciao/, src/, objs/)
REM   2. Copy headers and sources
REM   3. Generate configure.h, version.h, version.c, eng_build_info.c
REM   4. Compile configure -> generate configure.h
REM   5. Compile all engine .c files
REM   6. Link static ciao.exe
REM =========================================================================

setlocal enabledelayedexpansion

echo ============================================================
echo  Ciao Prolog - Windows Native Build
echo ============================================================
echo.

REM --- Locate project root ---
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%..\.."
set "PROJECT_ROOT=%CD%"
popd

echo Project root: %PROJECT_ROOT%

REM --- Ensure MinGW bin is in PATH (MSYS2 default install location) ---
if exist "C:\msys64\mingw64\bin" (
    set "PATH=C:\msys64\mingw64\bin;%PATH%"
)

REM --- Find compiler ---
set "CC="
where x86_64-w64-mingw32-gcc >nul 2>&1 && set "CC=x86_64-w64-mingw32-gcc"
if not defined CC (
    where gcc >nul 2>&1 && set "CC=gcc"
)
if not defined CC (
    echo ERROR: No MinGW-w64 or GCC found in PATH
    echo Install MinGW-w64 from https://www.mingw-w64.org/
    exit /b 1
)
echo Compiler: %CC%

REM --- Source directories ---
set "ENGINE_DIR=%PROJECT_ROOT%\core\engine"
set "BOOTSTRAP_DIR=%PROJECT_ROOT%\core\bootstrap\ciaoc.car"

REM --- Build directory layout (mimics cardir) ---
set "CARDIR=%PROJECT_ROOT%\build\windows-native"
set "BLD_HDIR=%CARDIR%\include"
set "BLD_CDIR=%CARDIR%\src"
set "BLD_OBJDIR=%CARDIR%\objs"
set "BLD_BINDIR=%CARDIR%\bin"
set "DIST_DIR=%PROJECT_ROOT%\dist\windows-static"

echo Build dir: %CARDIR%
echo.

REM --- Create directories ---
if not exist "%BLD_HDIR%\ciao" mkdir "%BLD_HDIR%\ciao"
if not exist "%BLD_CDIR%" mkdir "%BLD_CDIR%"
if not exist "%BLD_OBJDIR%" mkdir "%BLD_OBJDIR%"
if not exist "%BLD_BINDIR%" mkdir "%BLD_BINDIR%"

REM =========================================================
REM  Step 1: Copy headers to include/ciao/
REM =========================================================
echo [1/7] Copying headers to include\ciao\ ...
for %%f in ("%ENGINE_DIR%\*.h") do (
    copy /y "%%f" "%BLD_HDIR%\ciao\%%~nxf" >nul
)
REM Copy ciao_prolog.h one level up (NOALIAS)
if exist "%ENGINE_DIR%\ciao_prolog.h" (
    copy /y "%ENGINE_DIR%\ciao_prolog.h" "%BLD_HDIR%\ciao_prolog.h" >nul
)
REM Copy bootstrap headers
if exist "%BOOTSTRAP_DIR%\absmachdef.h" (
    copy /y "%BOOTSTRAP_DIR%\absmachdef.h" "%BLD_HDIR%\ciao\absmachdef.h" >nul
)
if exist "%BOOTSTRAP_DIR%\instrdefs.h" (
    copy /y "%BOOTSTRAP_DIR%\instrdefs.h" "%BLD_HDIR%\ciao\instrdefs.h" >nul
)
echo   Done.

REM =========================================================
REM  Step 2: Copy sources to src/
REM =========================================================
echo [2/7] Copying sources to src\ ...
REM Engine .c files
for %%f in ("%ENGINE_DIR%\*.c") do (
    copy /y "%%f" "%BLD_CDIR%\%%~nxf" >nul
)
REM Bootstrap wamloop.c
if exist "%BOOTSTRAP_DIR%\wamloop.c" (
    copy /y "%BOOTSTRAP_DIR%\wamloop.c" "%BLD_CDIR%\wamloop.c" >nul
)
REM Win32 patches (supplementary .c files)
set "PATCH_DIR=%PROJECT_ROOT%\windows\patches"
if exist "%PATCH_DIR%" (
    for %%f in ("%PATCH_DIR%\*.c") do (
        copy /y "%%f" "%BLD_CDIR%\%%~nxf" >nul
    )
    REM Also copy win32_native.h to include/ciao/
    if exist "%PATCH_DIR%\win32_native.h" (
        copy /y "%PATCH_DIR%\win32_native.h" "%BLD_HDIR%\ciao\win32_native.h" >nul
    )
)
echo   Done.

REM =========================================================
REM  Step 3: Generate version.h and version.c
REM =========================================================
echo [3/7] Generating version files ...
set "CIAO_MAJOR=1"
set "CIAO_MINOR=25"
set "CIAO_PATCH=0"
if exist "%PROJECT_ROOT%\core\Manifest\GlobalVersion" (
    set /p GVER=<"%PROJECT_ROOT%\core\Manifest\GlobalVersion"
    for /f "tokens=1,2 delims=." %%a in ("!GVER!") do (
        set "CIAO_MAJOR=%%a"
        set "CIAO_MINOR=%%b"
    )
)
if exist "%PROJECT_ROOT%\core\Manifest\GlobalPatch" (
    set /p CIAO_PATCH=<"%PROJECT_ROOT%\core\Manifest\GlobalPatch"
)
echo   Version: !CIAO_MAJOR!.!CIAO_MINOR!.!CIAO_PATCH!

REM version.h
(
    echo #ifndef _CIAO_VERSION_H
    echo #define _CIAO_VERSION_H
    echo #define CIAO_VERSION_STRING "Ciao !CIAO_MAJOR!.!CIAO_MINOR!.!CIAO_PATCH!"
    echo #define CIAO_MAJOR_VERSION !CIAO_MAJOR!
    echo #define CIAO_MINOR_VERSION !CIAO_MINOR!
    echo #define CIAO_PATCH_NUMBER !CIAO_PATCH!
    echo #endif /* _CIAO_VERSION_H */
) > "%BLD_HDIR%\ciao\version.h"

REM version.c
(
    echo char *ciao_version = "!CIAO_MAJOR!.!CIAO_MINOR!";
    echo char *ciao_patch = "!CIAO_PATCH!";
    echo char *ciao_commit_branch = "unknown";
    echo char *ciao_commit_id = "unknown";
    echo char *ciao_commit_date = "unknown";
    echo char *ciao_commit_desc = "unknown";
) > "%BLD_CDIR%\version.c"

REM eng_build_info.c
(
    echo char *eng_architecture = "x86_64";
    echo char *eng_os = "Win32";
    echo char *exec_suffix = ".exe";
    echo char *so_suffix = ".dll";
    echo char *eng_debug_level = "nodebug";
    echo int eng_is_sharedlib = 0;
    echo char *ciao_suffix = ".cpx";
    echo char *default_ciaoroot = "";
    echo char *default_c_headers_dir = "";
    echo char *foreign_opts_cc = "%CC%";
    echo char *foreign_opts_ld = "%CC%";
    echo char *foreign_opts_ccshared = "";
    echo char *foreign_opts_ldshared = "";
) > "%BLD_OBJDIR%\eng_build_info.c"

REM eng_static_mod.c (empty placeholder)
echo. > "%BLD_CDIR%\eng_static_mod.c"

echo   Done.

REM =========================================================
REM  Step 4: Build and run configure to generate configure.h
REM =========================================================
echo [4/7] Generating configure.h ...

set "CDEFS=-DWIN32_NATIVE -D_WIN32_WINNT=0x0601 -DWin32 -Dx86_64 -DFOREIGN_FILES -DUSE_OWN_MALLOC"
set "BASE_CFLAGS=-Wall -Wstrict-prototypes -std=gnu11 -O2"
set "INC_FLAGS=-I"%BLD_HDIR%""
set "ENGINE_CFLAGS=%BASE_CFLAGS% %CDEFS% %INC_FLAGS%"

REM Build configure.exe from the engine's self-configuration sources,
REM then run it to detect platform settings (endianness, malloc bases, etc.)
%CC% %ENGINE_CFLAGS% -DUSE_OWN_MALLOC -o "%BLD_OBJDIR%\configure.exe" "%BLD_CDIR%\configure.c" "%BLD_CDIR%\configure_main.c" "%BLD_CDIR%\own_mmap.c" "%BLD_CDIR%\win32_mman.c" -lkernel32
if !ERRORLEVEL! neq 0 (
    echo   WARNING: Could not build configure.exe, using defaults
    goto :use_default_configure
)

REM Run configure to generate the header
"%BLD_OBJDIR%\configure.exe" > "%BLD_HDIR%\ciao\configure_auto.h" 2>nul
if !ERRORLEVEL! neq 0 (
    echo   WARNING: configure.exe failed, using defaults
    goto :use_default_configure
)

REM Prepend mandatory build flags to configure.h
REM NOTE: We use ^! to escape ! from delayed expansion
(
    echo #if ^!defined^(Win32^)
    echo #define Win32
    echo #endif
    echo #if ^!defined^(x86_64^)
    echo #define x86_64
    echo #endif
    echo #if ^!defined^(FOREIGN_FILES^)
    echo #define FOREIGN_FILES
    echo #endif
    echo #if ^!defined^(USE_OWN_MALLOC^)
    echo #define USE_OWN_MALLOC
    echo #endif
    type "%BLD_HDIR%\ciao\configure_auto.h"
) > "%BLD_HDIR%\ciao\configure.h"
del "%BLD_HDIR%\ciao\configure_auto.h" 2>nul
echo   configure.h generated from configure.exe
goto :configure_done

:use_default_configure
REM Use safe defaults when configure.exe cannot run
(
    echo /* Pre-configured for Win32 x86_64 */
    echo #if ^!defined^(Win32^)
    echo #define Win32
    echo #endif
    echo #if ^!defined^(x86_64^)
    echo #define x86_64
    echo #endif
    echo #if ^!defined^(FOREIGN_FILES^)
    echo #define FOREIGN_FILES
    echo #endif
    echo #if ^!defined^(USE_OWN_MALLOC^)
    echo #define USE_OWN_MALLOC
    echo #endif
    echo #define IS_BIG_ENDIAN 0
    echo #define MallocBase5 0x0
    echo #define MIN_MEM_ALLOC_5 0x4000
    echo #define MallocBase4 0x0
    echo #define MIN_MEM_ALLOC_4 0x4000
    echo #define MallocBase3 0x0
    echo #define MIN_MEM_ALLOC_3 0x4000
    echo #define MallocBase2 0x0
    echo #define MIN_MEM_ALLOC_2 0x4000
    echo #define MallocBase1 0x0
    echo #define MIN_MEM_ALLOC_1 0x4000
    echo #define MallocBase0 0x0
    echo #define MIN_MEM_ALLOC_0 0x4000
    echo #if !defined^(USE_OWN_MALLOC^)
    echo #define USE_OWN_MALLOC 1
    echo #endif
) > "%BLD_HDIR%\ciao\configure.h"
echo   configure.h generated with defaults

:configure_done
echo.

REM =========================================================
REM  Step 5: Compile engine sources
REM =========================================================
echo [5/7] Compiling engine ...

REM Static build flags
set "STATIC_FLAGS=-static -static-libgcc"
set "ALL_CFLAGS=%ENGINE_CFLAGS% %STATIC_FLAGS% -D__USE_W32_SOCKETS"

REM List of engine .c files (from eng_info_mk)
set "ENG_CFILES=basiccontrol io_basic rune term_compare debugger_support"
set "ENG_CFILES=%ENG_CFILES% rt_exp runtime_control dynamic_rt stream_basic"
set "ENG_CFILES=%ENG_CFILES% timing arithmetic system system_info attributes"
set "ENG_CFILES=%ENG_CFILES% modload internals concurrency own_malloc own_mmap"
set "ENG_CFILES=%ENG_CFILES% eng_alloc eng_gc eng_registry terms_check"
set "ENG_CFILES=%ENG_CFILES% atomic_basic term_typing term_basic qread"
set "ENG_CFILES=%ENG_CFILES% eng_debug eng_profile eng_interrupt gauge"
set "ENG_CFILES=%ENG_CFILES% eng_bignum dtoa_ryu ciao_prolog eng_start"
REM wamloop.c is #included by basiccontrol.c -- do NOT compile separately
REM fix_size.c is a standalone utility, not compiled into the engine
set "ENG_CFILES=%ENG_CFILES% version"
REM win32_mman is needed for mmap emulation on Windows
set "ENG_CFILES=%ENG_CFILES% win32_mman"
REM Win32 native patches
set "ENG_CFILES=%ENG_CFILES% io_basic_win32 signals_win32 process_win32 system_win32"
REM eng_build_info is in objdir
set "ENG_CFILES_EXTRA=eng_build_info"

set "OBJ_LIST="
set "COMPILE_ERRORS=0"
set "COMPILE_LOG=%CARDIR%\compile_errors.log"
if exist "%COMPILE_LOG%" del /q "%COMPILE_LOG%" >nul 2>&1

REM Compile each engine source
for %%f in (%ENG_CFILES%) do (
    set "SRC=%BLD_CDIR%\%%f.c"
    set "OBJ=%BLD_OBJDIR%\%%f.o"
    if exist "!SRC!" (
        echo   [CC] %%f.c
        echo [CC] %%f.c>> "%COMPILE_LOG%"
        %CC% -c %ALL_CFLAGS% -o "!OBJ!" "!SRC!" >> "%COMPILE_LOG%" 2>&1
        if !ERRORLEVEL! neq 0 (
            echo   ** FAILED: %%f.c
            set /a COMPILE_ERRORS+=1
        ) else (
            set "OBJ_LIST=!OBJ_LIST! "!OBJ!""
        )
    ) else (
        echo   [SKIP] %%f.c not found
    )
)

REM Compile eng_build_info.c from objdir
echo   [CC] eng_build_info.c
echo [CC] eng_build_info.c>> "%COMPILE_LOG%"
%CC% -c %ALL_CFLAGS% -o "%BLD_OBJDIR%\eng_build_info.o" "%BLD_OBJDIR%\eng_build_info.c" >> "%COMPILE_LOG%" 2>&1
if !ERRORLEVEL! neq 0 (
    echo   ** FAILED: eng_build_info.c
    set /a COMPILE_ERRORS+=1
) else (
    set "OBJ_LIST=!OBJ_LIST! "%BLD_OBJDIR%\eng_build_info.o""
)

REM Compile eng_main.c (stub main)
echo   [MAIN] eng_main.c
echo [MAIN] eng_main.c>> "%COMPILE_LOG%"
%CC% -c %ALL_CFLAGS% -o "%BLD_OBJDIR%\eng_main.o" "%BLD_CDIR%\eng_main.c" >> "%COMPILE_LOG%" 2>&1
if !ERRORLEVEL! neq 0 (
    echo   ** FAILED: eng_main.c
    set /a COMPILE_ERRORS+=1
)

echo.
if !COMPILE_ERRORS! gtr 0 (
    echo WARNING: !COMPILE_ERRORS! file^(s^) failed to compile
)

REM =========================================================
REM  Step 6: Link ciao.exe
REM =========================================================
echo [6/7] Linking ciao.exe ...

set "LINK_LIBS=-lws2_32 -lshlwapi -ladvapi32 -lkernel32 -lshell32"
set "LINK_FLAGS=-static -Wl,--enable-auto-import"

%CC% %LINK_FLAGS% -o "%BLD_BINDIR%\ciao.exe" "%BLD_OBJDIR%\eng_main.o" %OBJ_LIST% %LINK_LIBS%
if !ERRORLEVEL! neq 0 (
    echo ERROR: Linking failed
    exit /b 1
)
echo   ciao.exe created successfully
echo.

REM =========================================================
REM  Step 7: Prepare distribution
REM =========================================================
echo [7/7] Preparing distribution ...

if not exist "%DIST_DIR%\bin" mkdir "%DIST_DIR%\bin"
if not exist "%DIST_DIR%\lib" mkdir "%DIST_DIR%\lib"

copy /y "%BLD_BINDIR%\ciao.exe" "%DIST_DIR%\bin\ciao.exe" >nul
if exist "%PROJECT_ROOT%\COPYING" copy /y "%PROJECT_ROOT%\COPYING" "%DIST_DIR%\LICENSE.txt" >nul
if exist "%PROJECT_ROOT%\COPYING.LESSER" copy /y "%PROJECT_ROOT%\COPYING.LESSER" "%DIST_DIR%\LICENSE-LESSER.txt" >nul

REM Copy Prolog libraries
if exist "%PROJECT_ROOT%\core\lib" (
    xcopy /s /e /y /q "%PROJECT_ROOT%\core\lib" "%DIST_DIR%\lib\core\" >nul 2>&1
)
if exist "%PROJECT_ROOT%\core\library" (
    xcopy /s /e /y /q "%PROJECT_ROOT%\core\library" "%DIST_DIR%\lib\library\" >nul 2>&1
)

echo   Done.
echo.
echo ============================================================
echo  Build complete!
echo  Binary: %BLD_BINDIR%\ciao.exe
echo  Distribution: %DIST_DIR%\
echo ============================================================
echo.

REM Quick test
"%BLD_BINDIR%\ciao.exe" --version 2>nul
if !ERRORLEVEL! equ 0 (
    echo [PASS] ciao.exe --version
) else (
    echo [INFO] ciao.exe returned non-zero ^(may need boot file^)
)

REM =========================================================
REM  Step 8: Bootstrap Prolog compiler and toplevel
REM =========================================================
echo.
echo [8/9] Bootstrapping Prolog system ...

REM Bootstrap the Prolog compiler and interactive toplevel.
REM Uses the prebuilt ciaoc.sta (bootstrap .car archive) to compile
REM the builder, toplevel (ciaosh), and compiler itself.
set "BOOT_STA=%PROJECT_ROOT%\core\bootstrap\ciaoc.car\ciaoc.sta"
set "CACHE_DIR=%PROJECT_ROOT%\build\cache"
set "HEADER_FILE=%PROJECT_ROOT%\core\lib\compiler\header"

REM Create build/cache directory for out-of-tree compilation
if not exist "%CACHE_DIR%" mkdir "%CACHE_DIR%"

REM Create empty compiler/header if missing (used by standalone exec generation)
if not exist "%HEADER_FILE%" type nul > "%HEADER_FILE%"

REM Set environment for bootstrap
set "CIAOROOT=%PROJECT_ROOT:\=/%"
set "CIAOPATH=%CIAOROOT%"
set "CIAOALIASPATH=ciaobld=%CIAOROOT%/builder/src"
set "CIAOHDIR=%BLD_DIR:\=/%/include"
set "CIAOENGINE=%BLD_BINDIR:\=/%/ciao.exe"

echo   Compiling ciao_builder ...
"%BLD_BINDIR%\ciao.exe" -x -s -o "%BLD_BINDIR%\ciao_builder.sta" "%PROJECT_ROOT%\builder\cmds\ciao_builder" -C -b "%BOOT_STA%"
if !ERRORLEVEL! neq 0 (
    echo ERROR: ciao_builder compilation failed
    exit /b 1
)
echo   ciao_builder.sta created

echo   Compiling ciaosh ^(toplevel^) ...
"%BLD_BINDIR%\ciao.exe" -x -s -o "%BLD_BINDIR%\ciaosh.sta" "%PROJECT_ROOT%\core\cmds\ciaosh" -C -b "%BOOT_STA%"
if !ERRORLEVEL! neq 0 (
    echo ERROR: ciaosh compilation failed
    exit /b 1
)
echo   ciaosh.sta created

echo   Compiling ciaoc ^(compiler^) ...
"%BLD_BINDIR%\ciao.exe" -x -s -o "%BLD_BINDIR%\ciaoc_fresh.sta" "%PROJECT_ROOT%\core\cmds\ciaoc" -C -b "%BOOT_STA%"
if !ERRORLEVEL! neq 0 (
    echo   WARNING: ciaoc compilation failed ^(using bootstrap ciaoc^)
    copy /y "%BOOT_STA%" "%BLD_BINDIR%\ciaoc_fresh.sta" >nul
)

REM =========================================================
REM  Step 9: Create wrapper scripts and finalize distribution
REM =========================================================
echo.
echo [9/9] Creating wrapper scripts ...

REM Copy .sta files to distribution
copy /y "%BLD_BINDIR%\ciaosh.sta" "%DIST_DIR%\bin\" >nul
copy /y "%BLD_BINDIR%\ciao_builder.sta" "%DIST_DIR%\bin\" >nul
if exist "%BLD_BINDIR%\ciaoc_fresh.sta" copy /y "%BLD_BINDIR%\ciaoc_fresh.sta" "%DIST_DIR%\bin\ciaoc.sta" >nul

REM Copy engine Prolog sources needed at runtime
if exist "%PROJECT_ROOT%\core\engine" (
    xcopy /s /e /y /q "%PROJECT_ROOT%\core\engine\*.pl" "%DIST_DIR%\lib\engine\" >nul 2>&1
)
REM Copy builder sources (for ciaobld alias path)
if exist "%PROJECT_ROOT%\builder\src" (
    xcopy /s /e /y /q "%PROJECT_ROOT%\builder\src" "%DIST_DIR%\lib\builder\src\" >nul 2>&1
)
REM Copy builder cmds
if exist "%PROJECT_ROOT%\builder\cmds" (
    xcopy /s /e /y /q "%PROJECT_ROOT%\builder\cmds" "%DIST_DIR%\lib\builder\cmds\" >nul 2>&1
)

REM Create ciaosh.bat wrapper
(
echo @echo off
echo set "CIAO_DIR=%%~dp0.."
echo set "CIAOROOT=%%CIAO_DIR:\=/%%"
echo set "CIAOPATH=%%CIAOROOT%%"
echo "%%~dp0ciao.exe" %%* -C -b "%%~dp0ciaosh.sta"
) > "%DIST_DIR%\bin\ciaosh.bat"

REM Create ciao.bat wrapper (default to toplevel)
(
echo @echo off
echo set "CIAO_DIR=%%~dp0.."
echo set "CIAOROOT=%%CIAO_DIR:\=/%%"
echo set "CIAOPATH=%%CIAOROOT%%"
echo set "CIAOALIASPATH=ciaobld=%%CIAOROOT%%/builder/src"
echo if "%%1"=="" goto toplevel
echo if "%%1"=="toplevel" goto toplevel
echo "%%~dp0ciao.exe" %%* -C -b "%%~dp0ciao_builder.sta"
echo goto end
echo :toplevel
echo "%%~dp0ciao.exe" -C -b "%%~dp0ciaosh.sta"
echo :end
) > "%DIST_DIR%\bin\ciao.bat"

echo   Done.

echo.
echo ============================================================
echo  Build complete^!
echo  Engine:    %BLD_BINDIR%\ciao.exe
echo  Toplevel:  %BLD_BINDIR%\ciaosh.sta
echo  Builder:   %BLD_BINDIR%\ciao_builder.sta
echo  Dist:      %DIST_DIR%\
echo ============================================================
echo.
echo  Quick test:
echo    "%DIST_DIR%\bin\ciaosh.bat"
echo    ?- write^('Hello'^), nl, halt.
echo.

endlocal
exit /b 0
