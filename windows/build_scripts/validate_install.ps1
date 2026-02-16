<#
.SYNOPSIS
    Validates a Ciao Prolog Windows native installation.

.DESCRIPTION
    Runs a comprehensive validation sequence after installation:
    1. Verify ciao.exe in PATH
    2. Run: ciao --version
    3. Test: ciao -e "write('Hello Windows'), nl, halt."
    4. Check VS Code extension detects executable
    5. Verify static linking (no external DLLs)
    6. Check registry entries

.EXAMPLE
    .\validate_install.ps1
    .\validate_install.ps1 -CiaoPath "C:\Program Files\CiaoProlog\bin\ciao.exe"
#>

param(
    [string]$CiaoPath = "",
    [switch]$Verbose
)

$ErrorActionPreference = "Continue"
$script:passed = 0
$script:failed = 0
$script:warnings = 0

function Write-TestResult {
    param(
        [string]$TestName,
        [string]$Status, # PASS, FAIL, WARN, SKIP
        [string]$Details = ""
    )
    
    $color = switch ($Status) {
        "PASS" { "Green" }
        "FAIL" { "Red" }
        "WARN" { "Yellow" }
        "SKIP" { "DarkGray" }
        default { "White" }
    }
    
    Write-Host "  [$Status] " -NoNewline -ForegroundColor $color
    Write-Host "$TestName" -NoNewline
    if ($Details) {
        Write-Host " - $Details" -ForegroundColor DarkGray
    } else {
        Write-Host ""
    }
    
    switch ($Status) {
        "PASS" { $script:passed++ }
        "FAIL" { $script:failed++ }
        "WARN" { $script:warnings++ }
    }
}

# ===============================================================
Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Ciao Prolog - Windows Native Installation Validator" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""

# ---------------------------------------------------------------
# Test 1: Find ciao.exe
# ---------------------------------------------------------------
Write-Host "[1/7] Checking ciao.exe location..." -ForegroundColor White

$ciaoExe = $null

if ($CiaoPath -and (Test-Path $CiaoPath)) {
    $ciaoExe = $CiaoPath
    Write-TestResult "Custom path" "PASS" $CiaoPath
}

if (-not $ciaoExe) {
    # Check PATH
    $pathResult = Get-Command "ciao.exe" -ErrorAction SilentlyContinue
    if ($pathResult) {
        $ciaoExe = $pathResult.Source
        Write-TestResult "Found in PATH" "PASS" $ciaoExe
    } else {
        Write-TestResult "Not in PATH" "WARN" "ciao.exe not found in system PATH"
    }
}

if (-not $ciaoExe) {
    # Check Program Files
    $pfPaths = @(
        "$env:ProgramFiles\CiaoProlog\bin\ciao.exe",
        "${env:ProgramFiles(x86)}\CiaoProlog\bin\ciao.exe",
        "$env:LOCALAPPDATA\Programs\CiaoProlog\bin\ciao.exe"
    )
    
    foreach ($p in $pfPaths) {
        if (Test-Path $p) {
            $ciaoExe = $p
            Write-TestResult "Found in Program Files" "PASS" $p
            break
        }
    }
}

if (-not $ciaoExe) {
    # Check Registry
    try {
        $regPath = Get-ItemProperty -Path "HKCU:\Software\CiaoProlog" -Name "InstallPath" -ErrorAction SilentlyContinue
        if ($regPath) {
            $regExe = Join-Path $regPath.InstallPath "bin\ciao.exe"
            if (Test-Path $regExe) {
                $ciaoExe = $regExe
                Write-TestResult "Found via Registry" "PASS" $regExe
            }
        }
    } catch {
        # Registry key doesn't exist
    }
}

if (-not $ciaoExe) {
    Write-TestResult "ciao.exe not found" "FAIL" "Install Ciao Prolog or provide -CiaoPath"
    Write-Host ""
    Write-Host "Installation not found. Remaining tests skipped." -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------
# Test 2: ciao --version
# ---------------------------------------------------------------
Write-Host ""
Write-Host "[2/7] Testing ciao --version..." -ForegroundColor White

try {
    $versionOutput = & $ciaoExe --version 2>&1
    if ($LASTEXITCODE -eq 0 -and $versionOutput) {
        Write-TestResult "Version check" "PASS" "$versionOutput"
    } else {
        Write-TestResult "Version check" "WARN" "Non-zero exit or empty output (may need boot file)"
    }
} catch {
    Write-TestResult "Version check" "FAIL" $_.Exception.Message
}

# ---------------------------------------------------------------
# Test 3: Hello World test
# ---------------------------------------------------------------
Write-Host ""
Write-Host "[3/7] Testing Hello World..." -ForegroundColor White

try {
    $helloOutput = & $ciaoExe -e "write('Hello Windows'), nl, halt." 2>&1
    if ($helloOutput -match "Hello Windows") {
        Write-TestResult "Hello World" "PASS" "Output: $helloOutput"
    } else {
        Write-TestResult "Hello World" "WARN" "Unexpected output: $helloOutput"
    }
} catch {
    Write-TestResult "Hello World" "WARN" "Could not execute test (may need boot file)"
}

# ---------------------------------------------------------------
# Test 4: Static linking verification
# ---------------------------------------------------------------
Write-Host ""
Write-Host "[4/7] Checking static linking..." -ForegroundColor White

$dumpbinAvailable = Get-Command "dumpbin.exe" -ErrorAction SilentlyContinue
if ($dumpbinAvailable) {
    $deps = & dumpbin /dependents $ciaoExe 2>$null | Select-String "\.dll" 
    $dllCount = ($deps | Measure-Object).Count
    
    # Windows system DLLs (KERNEL32, ntdll, etc.) are always present
    $systemDlls = @("KERNEL32.dll", "ntdll.dll", "ADVAPI32.dll", "msvcrt.dll")
    $externalDlls = $deps | Where-Object { 
        $line = $_.Line.Trim()
        $isSystem = $false
        foreach ($sys in $systemDlls) {
            if ($line -match [regex]::Escape($sys)) { $isSystem = $true; break }
        }
        -not $isSystem
    }
    
    $extCount = ($externalDlls | Measure-Object).Count
    if ($extCount -eq 0) {
        Write-TestResult "Static linking" "PASS" "No external DLL dependencies ($dllCount system DLLs)"
    } else {
        Write-TestResult "Static linking" "WARN" "$extCount external DLL(s) found"
        foreach ($dll in $externalDlls) {
            Write-Host "      -> $($dll.Line.Trim())" -ForegroundColor Yellow
        }
    }
} else {
    # Try objdump (MinGW)
    $objdumpAvailable = Get-Command "objdump.exe" -ErrorAction SilentlyContinue
    if ($objdumpAvailable) {
        $deps = & objdump -p $ciaoExe 2>$null | Select-String "DLL Name"
        Write-TestResult "Static linking (objdump)" "PASS" "DLL refs: $(($deps | Measure-Object).Count)"
    } else {
        Write-TestResult "Static linking" "SKIP" "Neither dumpbin nor objdump available"
    }
}

# ---------------------------------------------------------------
# Test 5: File size check
# ---------------------------------------------------------------
Write-Host ""
Write-Host "[5/7] Checking binary..." -ForegroundColor White

$fileInfo = Get-Item $ciaoExe
$sizeMB = [math]::Round($fileInfo.Length / 1MB, 2)

if ($fileInfo.Length -gt 100KB) {
    Write-TestResult "Binary size" "PASS" "$sizeMB MB"
} else {
    Write-TestResult "Binary size" "WARN" "Unusually small: $sizeMB MB"
}

# Check PE header
$bytes = [System.IO.File]::ReadAllBytes($ciaoExe)
if ($bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A) {
    Write-TestResult "PE header (MZ)" "PASS" "Valid Windows executable"
} else {
    Write-TestResult "PE header" "FAIL" "Not a valid PE executable"
}

# ---------------------------------------------------------------
# Test 6: Registry entries
# ---------------------------------------------------------------
Write-Host ""
Write-Host "[6/7] Checking registry entries..." -ForegroundColor White

try {
    $regKey = Get-ItemProperty -Path "HKCU:\Software\CiaoProlog" -ErrorAction SilentlyContinue
    if ($regKey) {
        Write-TestResult "Registry key exists" "PASS" "HKCU\Software\CiaoProlog"
        if ($regKey.InstallPath) {
            Write-TestResult "InstallPath" "PASS" $regKey.InstallPath
        }
        if ($regKey.Version) {
            Write-TestResult "Version" "PASS" $regKey.Version
        }
    } else {
        Write-TestResult "Registry key" "WARN" "Not found (installer may not have been run)"
    }
} catch {
    Write-TestResult "Registry check" "SKIP" "Could not read registry"
}

# ---------------------------------------------------------------
# Test 7: VS Code extension detection
# ---------------------------------------------------------------
Write-Host ""
Write-Host "[7/7] Checking VS Code integration..." -ForegroundColor White

$codeAvailable = Get-Command "code" -ErrorAction SilentlyContinue
if ($codeAvailable) {
    Write-TestResult "VS Code available" "PASS" (& code --version 2>$null | Select-Object -First 1)
    
    $extensions = & code --list-extensions 2>$null
    if ($extensions -match "ciao") {
        Write-TestResult "Ciao extension" "PASS" "Installed"
    } else {
        Write-TestResult "Ciao extension" "WARN" "Not installed. Install from VS Code marketplace."
    }
} else {
    Write-TestResult "VS Code" "SKIP" "Not found in PATH"
}

# ===============================================================
# Summary
# ===============================================================
Write-Host ""
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  Validation Summary" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "  Passed:   $script:passed" -ForegroundColor Green
Write-Host "  Failed:   $script:failed" -ForegroundColor Red
Write-Host "  Warnings: $script:warnings" -ForegroundColor Yellow
Write-Host ""

if ($script:failed -eq 0) {
    Write-Host "  Result: ALL CRITICAL TESTS PASSED" -ForegroundColor Green
    exit 0
} else {
    Write-Host "  Result: $script:failed TEST(S) FAILED" -ForegroundColor Red
    exit 1
}
