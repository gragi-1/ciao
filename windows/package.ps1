<#
.SYNOPSIS
  Packages Ciao Prolog for Windows into a distributable zip file.
.DESCRIPTION
  Creates CiaoProlog-Windows-x64.zip containing:
    - bin/          (ciao.exe, ciaosh.bat, .sta files, signal_ciao.exe)
    - lib/          (Prolog libraries)
    - install.bat   (double-click installer)
    - install.ps1   (PowerShell installer)
    - README.txt    (instructions)
#>

$ErrorActionPreference = 'Stop'

$repoRoot  = (Resolve-Path "$PSScriptRoot\..").Path
$distDir   = "$repoRoot\dist\windows-static"
$buildBin  = "$repoRoot\build\windows-native\bin"
$outDir    = "$repoRoot\dist"
$zipName   = "CiaoProlog-Windows-x64.zip"
$stagingDir = "$env:TEMP\ciao-package-staging"

Write-Host "Packaging Ciao Prolog for Windows..." -ForegroundColor Cyan

# Clean staging
if (Test-Path $stagingDir) { Remove-Item $stagingDir -Recurse -Force }
New-Item $stagingDir -ItemType Directory | Out-Null

# Copy bin/
Copy-Item "$distDir\bin" "$stagingDir\bin" -Recurse

# Reorganize from build layout to engine-expected layout:
#   dist/lib/core/      -> core/lib/       (core library modules)
#   dist/lib/library/   -> core/library/   (standard library)
#   dist/lib/engine/    -> core/engine/    (engine support files)
#   dist/lib/builder/   -> builder/        (build system)
New-Item "$stagingDir\core\lib" -ItemType Directory -Force | Out-Null
New-Item "$stagingDir\core\library" -ItemType Directory -Force | Out-Null
New-Item "$stagingDir\core\engine" -ItemType Directory -Force | Out-Null

Copy-Item "$distDir\lib\core\*" "$stagingDir\core\lib" -Recurse
Copy-Item "$distDir\lib\library\*" "$stagingDir\core\library" -Recurse
Copy-Item "$distDir\lib\engine\*" "$stagingDir\core\engine" -Recurse
Copy-Item "$distDir\lib\builder" "$stagingDir\builder" -Recurse

# Create build/cache directory (engine writes compiled module cache here)
New-Item "$stagingDir\build\cache" -ItemType Directory -Force | Out-Null

# Add signal_ciao.exe
if (Test-Path "$buildBin\signal_ciao.exe") {
    Copy-Item "$buildBin\signal_ciao.exe" "$stagingDir\bin\signal_ciao.exe"
}

# Add lpdoc.sta (documentation generator)
if (Test-Path "$distDir\bin\lpdoc.sta") {
    Copy-Item "$distDir\bin\lpdoc.sta" "$stagingDir\bin\lpdoc.sta"
    Write-Host "  Included lpdoc.sta" -ForegroundColor Green
}

# Copy lpdoc source (needed at runtime for documentation commands)
$lpdocDir = "$repoRoot\lpdoc"
if (Test-Path $lpdocDir) {
    New-Item "$stagingDir\lpdoc\src" -ItemType Directory -Force | Out-Null
    New-Item "$stagingDir\lpdoc\lib" -ItemType Directory -Force | Out-Null
    New-Item "$stagingDir\lpdoc\etc" -ItemType Directory -Force | Out-Null
    Copy-Item "$lpdocDir\src\*.pl" "$stagingDir\lpdoc\src\" -Force
    Copy-Item "$lpdocDir\lib\*" "$stagingDir\lpdoc\lib\" -Recurse -Force
    Copy-Item "$lpdocDir\etc\*" "$stagingDir\lpdoc\etc\" -Force
    Write-Host "  Included lpdoc sources" -ForegroundColor Green
}

# Copy Manifest files (needed by ciao_builder rescan-bundles during install)
# Core
if (Test-Path "$repoRoot\core\Manifest\Manifest.pl") {
    New-Item "$stagingDir\core\Manifest" -ItemType Directory -Force | Out-Null
    Copy-Item "$repoRoot\core\Manifest\Manifest.pl" "$stagingDir\core\Manifest\" -Force
}
# Builder
if (Test-Path "$repoRoot\builder\Manifest\Manifest.pl") {
    New-Item "$stagingDir\builder\Manifest" -ItemType Directory -Force | Out-Null
    Copy-Item "$repoRoot\builder\Manifest\Manifest.pl" "$stagingDir\builder\Manifest\" -Force
}
# LPdoc
if (Test-Path "$lpdocDir\Manifest\Manifest.pl") {
    New-Item "$stagingDir\lpdoc\Manifest" -ItemType Directory -Force | Out-Null
    Copy-Item "$lpdocDir\Manifest\Manifest.pl" "$stagingDir\lpdoc\Manifest\" -Force
} elseif (Test-Path "$stagingDir\lpdoc") {
    # Write standard lpdoc manifest if lpdoc source exists but no Manifest
    New-Item "$stagingDir\lpdoc\Manifest" -ItemType Directory -Force | Out-Null
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    [System.IO.File]::WriteAllText("$stagingDir\lpdoc\Manifest\Manifest.pl", @"
:- bundle(lpdoc).
version('3.9.0').
depends([core]).
alias_paths([
    lpdoc = 'src',
    library = 'lib',
    lpdoc_etc = 'etc'
]).
cmd('lpdoc', [main='cmds/lpdoccl']).
lib('src').
lib('lib').
"@, $utf8NoBom)
    Write-Host "  Created lpdoc Manifest" -ForegroundColor Green
}

# Place SETTINGS_DEFAULT.pl where the extension expects it
New-Item "$stagingDir\bndls\lpdoc\etc" -ItemType Directory -Force | Out-Null
if (Test-Path "$lpdocDir\etc\SETTINGS_DEFAULT.pl") {
    Copy-Item "$lpdocDir\etc\SETTINGS_DEFAULT.pl" "$stagingDir\bndls\lpdoc\etc\SETTINGS_DEFAULT.pl"
    Write-Host "  Included SETTINGS_DEFAULT.pl" -ForegroundColor Green
}

# Copy installer scripts
Copy-Item "$PSScriptRoot\install.ps1" "$stagingDir\install.ps1"
Copy-Item "$PSScriptRoot\install.bat" "$stagingDir\install.bat"

# Create README
@"
Ciao Prolog for Windows (Native, x86_64)
=========================================

INSTALLATION
  Double-click install.bat
  or run in PowerShell:
    powershell -ExecutionPolicy Bypass -File install.ps1

This will:
  - Install Ciao to %LOCALAPPDATA%\CiaoProlog
  - Add it to your PATH (no admin needed)
  - Configure VS Code (if installed)
  - Patch the Ciao VS Code extension (if installed)

USAGE
  Open a terminal and type:  ciaosh
  Or open any .pl file in VS Code.

  FILE PATHS (IMPORTANT):
    In ciaosh / Prolog source, use forward slashes or double backslashes:
      ?- use_module('C:/Users/YourName/myfile.pl').       %% OK
      ?- use_module('C:\\Users\\YourName\\myfile.pl').     %% OK
      ?- use_module('C:\Users\YourName\myfile.pl').        %% WRONG!
    This is standard ISO Prolog behavior (backslash is an escape character).
    The VS Code extension handles this automatically.

DOCUMENTATION
  LPdoc documentation generation is fully supported.
  In VS Code: right-click a .pl file > Preview Documentation
  Or use Ctrl+Shift+P > "Ciao: Preview Documentation"
  From the command line:
    ciao.exe -t html myfile.pl -C -b "%LOCALAPPDATA%\CiaoProlog\bin\lpdoc.sta"

VS CODE EXTENSION
  If not installed yet:
    code --install-extension ciao-lang.ciao-prolog-vsc
  Then re-run install.bat to apply Windows patches.

INSTALLING ON ANOTHER COMPUTER
  1. Copy CiaoProlog-Windows-x64.zip to the target machine
  2. Extract the zip anywhere
  3. Double-click install.bat (or run install.ps1)
  4. Open a new terminal window (for PATH changes to take effect)
  5. Type: ciaosh
  No admin rights, Visual Studio, or MSYS2 are required.

UNINSTALL
  Delete %LOCALAPPDATA%\CiaoProlog
  Remove CiaoProlog from your PATH (Settings > Environment Variables)

LICENSE
  Ciao Prolog is distributed under LGPL. See https://ciao-lang.org
"@ | Set-Content "$stagingDir\README.txt" -Encoding UTF8

# Create zip
$zipPath = Join-Path $outDir $zipName
if (Test-Path $zipPath) { Remove-Item $zipPath }
Compress-Archive -Path "$stagingDir\*" -DestinationPath $zipPath -CompressionLevel Optimal

$size = [math]::Round((Get-Item $zipPath).Length / 1MB, 1)
Write-Host ""
Write-Host "  Package created: $zipPath ($size MB)" -ForegroundColor Green
Write-Host ""

# Cleanup staging
Remove-Item $stagingDir -Recurse -Force
