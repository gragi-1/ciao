<#
.SYNOPSIS
  Installs Ciao Prolog for Windows (native) and patches the VS Code extension.
.DESCRIPTION
  Run this script from the extracted Ciao zip folder. It will:
    1. Copy Ciao to %LOCALAPPDATA%\CiaoProlog
    2. Add it to the user PATH
    3. Configure VS Code settings (if VS Code is installed)
    4. Patch the official Ciao VS Code extension (if installed)
.NOTES
  No admin rights required. Everything installs under the current user.
#>

param(
    [string]$InstallDir = "$env:LOCALAPPDATA\CiaoProlog"
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host ""
Write-Host "  Ciao Prolog - Windows Installer" -ForegroundColor Cyan
Write-Host "  ================================" -ForegroundColor Cyan
Write-Host ""

# ── Step 1: Copy files ────────────────────────────────────────────
Write-Host "[1/4] Installing to $InstallDir ..." -ForegroundColor Yellow

if (Test-Path $InstallDir) {
    Write-Host "  Removing previous installation..."
    Remove-Item $InstallDir -Recurse -Force
}

# Detect whether dist/ subfolder exists (running from repo) or we're in the zip
$distDir = if (Test-Path "$scriptDir\bin\ciao.exe") {
    # Running from extracted zip (already has correct layout: bin/ core/ builder/)
    $scriptDir
} elseif (Test-Path "$scriptDir\dist\windows-static\bin\ciao.exe") {
    # Running from repo — needs layout reorganization
    "$scriptDir\dist\windows-static"
} elseif (Test-Path "$scriptDir\..\dist\windows-static\bin\ciao.exe") {
    # Running from windows/ subdir in repo
    (Resolve-Path "$scriptDir\..\dist\windows-static").Path
} else {
    Write-Error "Cannot find Ciao binaries. Run this script from the extracted zip folder."
    exit 1
}

# Check if source has the correct layout (core/lib/) or old flat layout (lib/core/)
$needsReorg = (Test-Path "$distDir\lib\core") -and -not (Test-Path "$distDir\core\lib")

if ($needsReorg) {
    # Reorganize: lib/core/ → core/lib/, lib/library/ → core/library/, etc.
    Write-Host "  Reorganizing directory layout..."
    New-Item $InstallDir -ItemType Directory -Force | Out-Null
    Copy-Item "$distDir\bin" "$InstallDir\bin" -Recurse -Force
    New-Item "$InstallDir\core\lib" -ItemType Directory -Force | Out-Null
    New-Item "$InstallDir\core\library" -ItemType Directory -Force | Out-Null
    New-Item "$InstallDir\core\engine" -ItemType Directory -Force | Out-Null
    Copy-Item "$distDir\lib\core\*" "$InstallDir\core\lib" -Recurse -Force
    Copy-Item "$distDir\lib\library\*" "$InstallDir\core\library" -Recurse -Force
    Copy-Item "$distDir\lib\engine\*" "$InstallDir\core\engine" -Recurse -Force
    Copy-Item "$distDir\lib\builder" "$InstallDir\builder" -Recurse -Force
} else {
    Copy-Item $distDir $InstallDir -Recurse -Force
}

# Ensure build/cache directory exists (engine writes compiled modules here)
New-Item "$InstallDir\build\cache" -ItemType Directory -Force | Out-Null

# Install lpdoc files (documentation generator)
# When running from repo, the lpdoc dir is at repo root; from zip it's already in place
$repoRoot = (Resolve-Path "$scriptDir\..").Path
$lpdocRepo = "$repoRoot\lpdoc"
if (-not (Test-Path "$InstallDir\lpdoc\src") -and (Test-Path "$lpdocRepo\src")) {
    Write-Host "  Installing LPdoc files..."
    New-Item "$InstallDir\lpdoc\src" -ItemType Directory -Force | Out-Null
    New-Item "$InstallDir\lpdoc\lib" -ItemType Directory -Force | Out-Null
    New-Item "$InstallDir\lpdoc\etc" -ItemType Directory -Force | Out-Null
    Copy-Item "$lpdocRepo\src\*.pl" "$InstallDir\lpdoc\src\" -Force
    Copy-Item "$lpdocRepo\lib\*" "$InstallDir\lpdoc\lib\" -Recurse -Force
    Copy-Item "$lpdocRepo\etc\*" "$InstallDir\lpdoc\etc\" -Force
}
if (-not (Test-Path "$InstallDir\bndls\lpdoc\etc\SETTINGS_DEFAULT.pl")) {
    $settingsFile = if (Test-Path "$lpdocRepo\etc\SETTINGS_DEFAULT.pl") { "$lpdocRepo\etc\SETTINGS_DEFAULT.pl" }
                    elseif (Test-Path "$InstallDir\lpdoc\etc\SETTINGS_DEFAULT.pl") { "$InstallDir\lpdoc\etc\SETTINGS_DEFAULT.pl" }
                    else { $null }
    if ($settingsFile) {
        New-Item "$InstallDir\bndls\lpdoc\etc" -ItemType Directory -Force | Out-Null
        Copy-Item $settingsFile "$InstallDir\bndls\lpdoc\etc\SETTINGS_DEFAULT.pl" -Force
    }
}

Write-Host "  Copied to $InstallDir" -ForegroundColor Green

# ── Step 1b: Generate bundle registry ─────────────────────────────
Write-Host "  Generating bundle registry..."
New-Item "$InstallDir\build\bundlereg" -ItemType Directory -Force | Out-Null

# Ensure Manifest directories exist for all bundles
# Core
if (-not (Test-Path "$InstallDir\core\Manifest\Manifest.pl") -and (Test-Path "$repoRoot\core\Manifest\Manifest.pl")) {
    New-Item "$InstallDir\core\Manifest" -ItemType Directory -Force | Out-Null
    Copy-Item "$repoRoot\core\Manifest\Manifest.pl" "$InstallDir\core\Manifest\" -Force
}
# Builder
if (-not (Test-Path "$InstallDir\builder\Manifest\Manifest.pl") -and (Test-Path "$repoRoot\builder\Manifest\Manifest.pl")) {
    New-Item "$InstallDir\builder\Manifest" -ItemType Directory -Force | Out-Null
    Copy-Item "$repoRoot\builder\Manifest\Manifest.pl" "$InstallDir\builder\Manifest\" -Force
}
# LPdoc — create from known content if not present
if (-not (Test-Path "$InstallDir\lpdoc\Manifest\Manifest.pl")) {
    $lpdocManifestSrc = if (Test-Path "$lpdocRepo\Manifest\Manifest.pl") { "$lpdocRepo\Manifest\Manifest.pl" } else { $null }
    New-Item "$InstallDir\lpdoc\Manifest" -ItemType Directory -Force | Out-Null
    if ($lpdocManifestSrc) {
        Copy-Item $lpdocManifestSrc "$InstallDir\lpdoc\Manifest\" -Force
    } else {
        # Write the standard lpdoc Manifest directly
        $utf8NoBom = New-Object System.Text.UTF8Encoding $false
        [System.IO.File]::WriteAllText("$InstallDir\lpdoc\Manifest\Manifest.pl", @"
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
    }
}

# Run rescan-bundles to generate bundlereg files
$builderSta = "$InstallDir\bin\ciao_builder.sta"
$ciaoExe = "$InstallDir\bin\ciao.exe"
if ((Test-Path $ciaoExe) -and (Test-Path $builderSta)) {
    $batContent = "@echo off`r`nset CIAOROOT=$InstallDir`r`ncd /d $InstallDir`r`n`"$ciaoExe`" rescan-bundles `"$InstallDir`" -C -b `"$builderSta`"`r`n"
    $batFile = "$env:TEMP\ciao_rescan_$PID.bat"
    [System.IO.File]::WriteAllText($batFile, $batContent, (New-Object System.Text.UTF8Encoding $false))
    $p = Start-Process -FilePath "cmd.exe" -ArgumentList "/c", $batFile -NoNewWindow -PassThru -Wait
    if ($p.ExitCode -eq 0) {
        $regCount = (Get-ChildItem "$InstallDir\build\bundlereg\*.bundlereg" -ErrorAction SilentlyContinue).Count
        Write-Host "  Bundle registry generated ($regCount bundles)." -ForegroundColor Green
    } else {
        Write-Host "  Warning: Bundle registry generation failed (exit $($p.ExitCode))." -ForegroundColor DarkYellow
        Write-Host "  LPdoc may not work. Set CIAOROOT=$InstallDir manually." -ForegroundColor DarkYellow
    }
    Remove-Item $batFile -ErrorAction SilentlyContinue
} else {
    Write-Host "  Warning: Cannot generate bundle registry (missing ciao.exe or ciao_builder.sta)." -ForegroundColor DarkYellow
}

# Also copy signal_ciao.exe if it exists
$sigExe = "$scriptDir\signal_ciao.exe"
if (-not (Test-Path $sigExe)) { $sigExe = "$scriptDir\..\build\windows-native\bin\signal_ciao.exe" }
if (Test-Path $sigExe) {
    Copy-Item $sigExe "$InstallDir\bin\signal_ciao.exe" -Force
}

# ── Step 2: Add to PATH ──────────────────────────────────────────
Write-Host "[2/4] Adding to PATH ..." -ForegroundColor Yellow

$binDir = "$InstallDir\bin"
$currentPath = [Environment]::GetEnvironmentVariable("Path", "User")
$pathEntries = $currentPath -split ";" | Where-Object { $_ -ne "" }

if ($pathEntries -contains $binDir) {
    Write-Host "  Already in PATH." -ForegroundColor Green
} else {
    # Remove any old CiaoProlog entries
    $pathEntries = $pathEntries | Where-Object { $_ -notmatch "CiaoProlog" }
    $newPath = ($pathEntries + $binDir) -join ";"
    [Environment]::SetEnvironmentVariable("Path", $newPath, "User")
    Write-Host "  Added $binDir to user PATH." -ForegroundColor Green
}

# ── Step 3: Configure VS Code ────────────────────────────────────
Write-Host "[3/4] Configuring VS Code ..." -ForegroundColor Yellow

$vsSettings = "$env:APPDATA\Code\User\settings.json"
if (Test-Path $vsSettings) {
    $json = Get-Content $vsSettings -Raw | ConvertFrom-Json

    # Set ciao.versions
    $ciaoPath = $InstallDir -replace '\\', '\\'
    $json | Add-Member -NotePropertyName 'ciao.versions' -NotePropertyValue @(
        [PSCustomObject]@{ name = "windows-native"; path = $InstallDir }
    ) -Force

    # Remove old executablePath if present
    if ($json.PSObject.Properties['ciao.executablePath']) {
        $json.PSObject.Properties.Remove('ciao.executablePath')
    }

    $json | ConvertTo-Json -Depth 5 | Set-Content $vsSettings -Encoding UTF8
    Write-Host "  VS Code settings updated." -ForegroundColor Green
} else {
    Write-Host "  VS Code not found (no settings.json). Skipping." -ForegroundColor DarkYellow
}

# ── Step 4: Patch VS Code extension ──────────────────────────────
Write-Host "[4/4] Patching VS Code extension ..." -ForegroundColor Yellow

$extPattern = "$env:USERPROFILE\.vscode\extensions\ciao-lang.ciao-prolog-vsc-*"
$extDirs = Get-ChildItem $extPattern -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending

if ($extDirs.Count -gt 0) {
    $extDir = $extDirs[0].FullName
    $jsFile = Join-Path $extDir 'out\client\src\extension.js'

    if (Test-Path $jsFile) {
        $bakFile = "$jsFile.bak"
        if (-not (Test-Path $bakFile)) {
            Copy-Item $jsFile $bakFile
        }

        $c = [System.IO.File]::ReadAllText($bakFile)
        $patchCount = 0

        function Apply-Patch {
            param([string]$Name, [string]$Old, [string]$New)
            if ($script:c.Contains($Old)) {
                $script:c = $script:c.Replace($Old, $New)
                $script:patchCount++
                Write-Host "    [OK] $Name" -ForegroundColor Green
            } else {
                Write-Host "    [SKIP] $Name" -ForegroundColor DarkYellow
            }
        }

        # PATCH 1: detectOS
        Apply-Patch 'detectOS' `
            'detectOS(){let e=process.platform;return e==="darwin"?"darwin":e==="linux"?ca.default.release().toLowerCase().includes("microsoft")?"wsl":"linux":"unknown"}' `
            'detectOS(){let e=process.platform;return e==="darwin"?"darwin":e==="win32"?"windows":e==="linux"?ca.default.release().toLowerCase().includes("microsoft")?"wsl":"linux":"unknown"}'

        # PATCH 2: commands map
        Apply-Patch 'commands map' `
            'unknown:{open:"false"}}' `
            'unknown:{open:"false"},windows:{open:"start"}}'

        # PATCH 3: spawn shell
        Apply-Patch 'spawn shell' `
            'a={shell:"/bin/bash",...t}' `
            'a={shell:process.platform==="win32"?"cmd.exe":"/bin/bash",...t}'

        # PATCH 4: loadPATHVersion
        Apply-Patch 'loadPATHVersion' `
            'async loadPATHVersion(){let e=["/bin/bash","/bin/zsh","/bin/csh"],t;for(let r of e){let{exitCode:i,stdout:s}=await wn("which",["ciao"],{shell:r});if(i===3){t=s;break}}t!==void 0&&(await this.addVersion("$PATH",yi.default.resolve(yi.default.dirname(t),"..","..")),this.setActiveVersion("$PATH"))}' `
            'async loadPATHVersion(){let t;if(process.platform==="win32"){let{exitCode:i,stdout:s}=await wn("where",["ciao.exe"],{shell:"cmd.exe"});if(i===3){t=s.split("\r\n")[0]}}else{let e=["/bin/bash","/bin/zsh","/bin/csh"];for(let r of e){let{exitCode:i,stdout:s}=await wn("which",["ciao"],{shell:r});if(i===3){t=s;break}}}if(t!==void 0){let proot;if(process.platform==="win32"){let d=yi.default.dirname(t),fs=require("fs"),c=d;for(let i=0;i<5;i++){if(fs.existsSync(yi.default.join(c,"core"))){proot=c;break}c=yi.default.dirname(c)}if(!proot)proot=yi.default.resolve(d,"..")}else{proot=yi.default.resolve(yi.default.dirname(t),"..","..")}await this.addVersion("$PATH",proot);this.setActiveVersion("$PATH")}}'

        # PATCH 5: loadCIAOROOTVersions
        Apply-Patch 'loadCIAOROOTVersions' `
            'async loadCIAOROOTVersions(){let e=yi.default.join((0,dh.homedir)(),".ciaoroot"),t;try{t=await lh.default.readdir(e,{withFileTypes:!0})}catch{throw new Error(`Could not read Ciao versions from \`${e}\`.`)}' `
            'async loadCIAOROOTVersions(){let e=yi.default.join((0,dh.homedir)(),".ciaoroot"),t;try{t=await lh.default.readdir(e,{withFileTypes:!0})}catch{if(process.platform==="win32")return;throw new Error(`Could not read Ciao versions from \`${e}\`.`)}'

        # PATCH 6: loadEnv
        Apply-Patch 'loadEnv' `
            'async loadEnv(){let e=oh.default.join(this._path,"build","bin","ciao-env");await sh.default.stat(e);let{exitCode:t,stdout:r}=await wn(e,["--sh"]);if(t!==3)throw new Error(`The \`ciao-env\` script of Ciao version "${this.name}" failed.`);return this._env=r.split(`' `
            'async loadEnv(){if(process.platform==="win32"){let b=this._path,bd;try{let wp=oh.default.join(b,"build","windows-native","bin","ciao.exe");await sh.default.stat(wp);bd=oh.default.join(b,"build","windows-native","bin")}catch{bd=oh.default.join(b,"bin")}let fr=b.replace(/\\/g,"/"),fd=bd.replace(/\\/g,"/");this._env={CIAOROOT:fr,CIAOPATH:fr,CIAOHDIR:oh.default.dirname(bd),CIAOENGINE:oh.default.join(bd,"ciao.exe"),CIAOALIASPATH:"ciaobld="+fr+"/builder/src;lpdoc="+fr+"/lpdoc/src;lpdoc_etc="+fr+"/lpdoc/etc;library="+fr+"/lpdoc/lib",PATH:bd+";"+(process.env.PATH||"")};return this}let e=oh.default.join(this._path,"build","bin","ciao-env");await sh.default.stat(e);let{exitCode:t,stdout:r}=await wn(e,["--sh"]);if(t!==3)throw new Error(`The \`ciao-env\` script of Ciao version "${this.name}" failed.`);return this._env=r.split(`'

        # PATCH 7: runCiaoCmd
        Apply-Patch 'runCiaoCmd' `
            'runCiaoCmd(e,t,r={}){if(!this.activeVersion)throw new Error("No active Ciao version has been selected.");let i={...r,env:{...process.env,...this.activeVersion.env}};return(0,uh.spawn)(e,t,i)}' `
            'runCiaoCmd(e,t,r={}){if(!this.activeVersion)throw new Error("No active Ciao version has been selected.");let i={...r,env:{...process.env,...this.activeVersion.env}};if(process.platform==="win32"&&e.toLowerCase().endsWith(".bat"))i.shell=true;return(0,uh.spawn)(e,t,i)}'

        # PATCH 8: shellQuote (here-strings needed because JS backticks conflict with PS escape char)
        $p8old = @'
function it(n){return`'${n.replace(/'/g,"\\'")}'`}
'@
        $p8new = @'
function it(n){if(process.platform==="win32"){n=n.replace(/\\/g,"/");return`'${n.replace(/'/g,"\\'")}'`}return`'${n.replace(/'/g,"\\'")}'`}
'@
        Apply-Patch 'shellQuote' $p8old $p8new

        # PATCH 9: validOS (handled implicitly by Patch 1 since it returns "windows" not "unknown")
        $vi = $c.IndexOf('validOS()')
        if ($vi -ge 0) { $patchCount++; Write-Host "    [OK] validOS (implicit)" -ForegroundColor Green }

        # PATCH 10: ma() path transform
        Apply-Patch 'ma() path' `
            'function ma(n){let e=n.split(Or.sep);return e[0].endsWith(":")?(e[0]=`/mnt/${e[0].slice(0,e[0].length-1)}`,e.join(Or.sep)):n}' `
            'function ma(n){if(process.platform==="win32")return n;let e=n.split(Or.sep);return e[0].endsWith(":")?(e[0]=`/mnt/${e[0].slice(0,e[0].length-1)}`,e.join(Or.sep)):n}'

        # PATCH 11: interrupt
        $installDirJS = $InstallDir -replace '\\', '\\\\'
        Apply-Patch 'interrupt' `
            'interrupt(){return new Promise(e=>{this.resolveCommand=e,this.cproc?.kill("SIGINT")})}' `
            'interrupt(){return new Promise(e=>{this.resolveCommand=e;if(process.platform==="win32"){const cp=require("child_process"),p=require("path"),binDir=p.dirname(this.ciao?.activeVersion?.env?.CIAOENGINE||""),sigExe=p.join(binDir,"signal_ciao.exe"),pid=String(this.cproc?.pid||0);try{cp.execFileSync(sigExe,[pid],{timeout:2000,windowsHide:true})}catch(x){const now=Date.now();if(this._winLastInt&&(now-this._winLastInt)<2000){this.cproc?.kill("SIGINT")}else{this._winLastInt=now;this.outputCallback("\r\n{ Press Ctrl+C again within 2s to terminate }\r\n")}}setTimeout(()=>{if(this.resolveCommand===e){e(void 0);this.resolveCommand=void 0}},500)}else{this.cproc?.kill("SIGINT")}})}'

        # PATCH 12: runCiaoSH
        Apply-Patch 'runCiaoSH' `
            'runCiaoSH(e){return this.runCiaoCmd("ciaosh",["-i"],e)}' `
            'runCiaoSH(e){if(process.platform==="win32"&&this.activeVersion){let bd=yi.default.dirname(this.activeVersion.env.CIAOENGINE||yi.default.join(this.activeVersion.path,"bin","ciao.exe"));return this.runCiaoCmd(yi.default.join(bd,"ciao.exe"),["-C","-b",yi.default.join(bd,"ciaosh.sta"),"-i"],e)}return this.runCiaoCmd("ciaosh",["-i"],e)}'

        # PATCH 13: handleExit hint
        Apply-Patch 'handleExit hint' `
            '`)};this.handleStdout=' `
            '`);if(process.platform==="win32")this.outputCallback("Press Enter to restart.\r\n")};this.handleStdout='

        # PATCH 14: handleInput restart
        Apply-Patch 'handleInput restart' `
            'handleInput(e){if(!this.isRunning())return;let t=' `
            'handleInput(e){if(!this.isRunning()){if(e==="\r"||e==="\n"){this.writeEmitter.fire("\r\nRestarting...\r\n");this.cproc.start().catch(function(){})}return}let t='

        # PATCH 15: runLPdoc
        Apply-Patch 'runLPdoc' `
            'runLPdoc(e){return this.runCiaoCmd("lpdoc",["-T","-i"],e)}' `
            'runLPdoc(e){if(process.platform==="win32"&&this.activeVersion){let bd=yi.default.dirname(this.activeVersion.env.CIAOENGINE||yi.default.join(this.activeVersion.path,"bin","ciao.exe"));return this.runCiaoCmd(yi.default.join(bd,"ciao.exe"),["-T","-i","-C","-i","-b",yi.default.join(bd,"lpdoc.sta")],e)}return this.runCiaoCmd("lpdoc",["-T","-i"],e)}'

        # PATCH 16: previewDoc symlink → copyFile on Windows
        $p16old = @'
await $f.default.symlink(s,u,"file")
'@
        $p16new = @'
await(process.platform==="win32"?$f.default.copyFile(s,u):$f.default.symlink(s,u,"file"))
'@
        Apply-Patch 'previewDoc symlink' $p16old $p16new

        [System.IO.File]::WriteAllText($jsFile, $c)
        Write-Host "  Extension patched ($patchCount patches)." -ForegroundColor Green
    }
} else {
    Write-Host "  Ciao VS Code extension not installed. Skipping." -ForegroundColor DarkYellow
    Write-Host "  Install it later: code --install-extension ciao-lang.ciao-prolog-vsc" -ForegroundColor DarkYellow
    Write-Host "  Then re-run this installer to patch it." -ForegroundColor DarkYellow
}

# ── Done ──────────────────────────────────────────────────────────
Write-Host ""
Write-Host "  Installation complete!" -ForegroundColor Green
Write-Host ""
Write-Host "  Usage:" -ForegroundColor Cyan
Write-Host "    Open a new terminal and type: ciaosh" -ForegroundColor White
Write-Host "    Or open any .pl file in VS Code" -ForegroundColor White
Write-Host ""
Write-Host "  To uninstall:" -ForegroundColor DarkYellow
Write-Host "    Remove $InstallDir" -ForegroundColor DarkYellow
Write-Host "    Remove CiaoProlog entry from PATH (System > Environment Variables)" -ForegroundColor DarkYellow
Write-Host ""
