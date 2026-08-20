<#
.SYNOPSIS
  Khora dev task runner. The build toolchain is not on PATH and the Visual Studio
  Installer metadata for C:\BuildTools is gone, so the VS generator cannot resolve
  the instance. This script activates MSVC directly from vcvars64.bat and drives
  CMake+Ninja, which needs no installer metadata.

.EXAMPLE
  .\tools\khora.ps1 build
  .\tools\khora.ps1 test
  .\tools\khora.ps1 all
  .\tools\khora.ps1 run khora.exe
#>
[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('env', 'configure', 'build', 'test', 'bench', 'tidy', 'format', 'run', 'clean', 'all')]
    [string]$Task = 'all',

    [Parameter(Position = 1, ValueFromRemainingArguments = $true)]
    [string[]]$Rest,

    [ValidateSet('Release', 'Debug', 'RelWithDebInfo')]
    [string]$Config = 'Release',

    # Ninja is single-config, so each build type needs its own tree or they
    # clobber each other. Release keeps the plain 'build' path the docs use.
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot

if (-not $BuildDir) {
    $BuildDir = if ($Config -eq 'Release') { 'build' } else { "build-$($Config.ToLower())" }
}

# --- toolchain discovery -----------------------------------------------------
# ponytail: candidate lists, not a full vswhere probe. Add a path here if the
# toolchain moves; vswhere is unavailable on this host (installer was removed).
$VcVarsCandidates = @(
    'C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat',
    'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
)
$CMakeBinCandidates = @(
    'C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin',
    'C:\Program Files\CMake\bin'
)
$NinjaBinCandidates = @(
    'C:\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja'
)

function Find-First([string[]]$Candidates, [string]$What) {
    foreach ($c in $Candidates) { if (Test-Path $c) { return $c } }
    throw "Could not locate $What. Tried:`n  $($Candidates -join "`n  ")`nAdd the correct path to tools\khora.ps1."
}

# Import the MSVC environment (INCLUDE/LIB/PATH) into this session. vcvars only
# speaks cmd, so run it there and copy the resulting variables back.
function Import-MsvcEnv {
    if ($env:KHORA_MSVC_READY -eq '1') { return }
    $vcvars = Find-First $VcVarsCandidates 'vcvars64.bat (MSVC toolchain)'
    Write-Verbose "Activating MSVC from $vcvars"
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2] -ErrorAction SilentlyContinue
        }
    }
    $env:PATH = "$(Find-First $CMakeBinCandidates 'cmake.exe');$(Find-First $NinjaBinCandidates 'ninja.exe');$env:PATH"
    Import-WindowsSdk
    $env:KHORA_MSVC_READY = '1'
}

# vcvars64 on this host leaves WindowsSDKVersion empty and INCLUDE/LIB without
# ucrt/um/shared: the SDK is on disk but not registered with the VS Installer,
# which was removed. Without this, cl.exe compiles fine but link.exe finds
# neither the CRT import libraries nor rc.exe/mt.exe. Wire it by hand.
function Import-WindowsSdk {
    $sdkRoot = 'C:\Program Files (x86)\Windows Kits\10'
    if (-not (Test-Path "$sdkRoot\Include")) {
        Write-Warning "Windows SDK not found at $sdkRoot; linking will fail."
        return
    }
    # Newest version directory that has both the headers and the x64 libs.
    $sdkVer = Get-ChildItem "$sdkRoot\Include" -Directory |
        Where-Object {
            $_.Name -match '^10\.' -and
            (Test-Path "$($_.FullName)\um\windows.h") -and
            (Test-Path "$sdkRoot\Lib\$($_.Name)\um\x64\kernel32.Lib")
        } |
        Sort-Object { [version]$_.Name } | Select-Object -Last 1
    if (-not $sdkVer) { Write-Warning "No complete Windows SDK under $sdkRoot."; return }

    $v = $sdkVer.Name
    if ($env:INCLUDE -like "*$v*") { return }   # already wired

    $env:INCLUDE = (@(
        "$sdkRoot\Include\$v\ucrt", "$sdkRoot\Include\$v\um",
        "$sdkRoot\Include\$v\shared", "$sdkRoot\Include\$v\winrt",
        "$sdkRoot\Include\$v\cppwinrt"
    ) -join ';') + ';' + $env:INCLUDE
    $env:LIB = (@(
        "$sdkRoot\Lib\$v\ucrt\x64", "$sdkRoot\Lib\$v\um\x64"
    ) -join ';') + ';' + $env:LIB
    $env:PATH = "$sdkRoot\bin\$v\x64;$env:PATH"   # rc.exe, mt.exe
    $env:WindowsSDKVersion = "$v\"
    $env:WindowsSdkDir = "$sdkRoot\"
    Write-Verbose "Wired Windows SDK $v"
}

# NB: parameter is $CmdArgs, not $Args -- $Args is a PowerShell automatic
# variable and shadowing it silently drops everything passed in.
function Invoke-Checked([string]$Exe, [string[]]$CmdArgs) {
    Write-Host "> $Exe $($CmdArgs -join ' ')" -ForegroundColor DarkCyan
    & $Exe @CmdArgs
    if ($LASTEXITCODE -ne 0) { throw "$Exe failed with exit code $LASTEXITCODE" }
}

$BuildPath = Join-Path $Root $BuildDir

function Task-Configure {
    Import-MsvcEnv
    Invoke-Checked cmake @(
        '-S', $Root, '-B', $BuildPath,
        '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$Config",
        '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl',
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
    )
}

function Task-Build {
    Import-MsvcEnv
    if (-not (Test-Path (Join-Path $BuildPath 'CMakeCache.txt'))) { Task-Configure }
    Invoke-Checked cmake @('--build', $BuildPath) 
}

# ctest's exit code is the signal the dev loop gates on, so it is stashed in a
# script-scope variable rather than returned: a `return` would be mixed into the
# same pipeline as ctest's own stdout and arrive at the caller as an array.
$script:TestExit = 0

function Task-Test {
    Import-MsvcEnv
    Write-Host "> ctest --test-dir $BuildPath --output-on-failure" -ForegroundColor DarkCyan
    $extra = if ($Rest) { $Rest } else { @() }
    & ctest --test-dir $BuildPath --output-on-failure @extra
    $script:TestExit = $LASTEXITCODE
}

function Task-Bench {
    Import-MsvcEnv
    Get-ChildItem (Join-Path $BuildPath 'bin') -Filter '*_bench.exe' -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "--- $($_.Name) ---" -ForegroundColor Yellow; & $_.FullName }
}

# clang-tidy and clang-format ship inside the Build Tools install; neither is on
# PATH. They need the same MSVC+SDK environment as the compiler, because in
# cl driver-mode clang reads system headers from INCLUDE.
function Get-LlvmTool([string]$Name) {
    $candidates = @(
        "C:\BuildTools\VC\Tools\Llvm\x64\bin\$Name.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\$Name.exe",
        "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin\$Name.exe",
        "C:\Program Files\LLVM\bin\$Name.exe"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { return $c } }
    $onPath = Get-Command $Name -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw "Could not locate $Name. Tried:`n  $($candidates -join "`n  ")`nand PATH."
}

function Task-Tidy {
    Import-MsvcEnv
    if (-not (Test-Path (Join-Path $BuildPath 'compile_commands.json'))) { Task-Configure }
    $tidy = Get-LlvmTool 'clang-tidy'
    $targets = if ($Rest) { $Rest } else { @('src', 'include') }
    $files = $targets | ForEach-Object {
        $p = if ([IO.Path]::IsPathRooted($_)) { $_ } else { Join-Path $Root $_ }
        if (Test-Path $p -PathType Leaf) { $p }
        else { Get-ChildItem $p -Recurse -Include '*.cpp' -File | Select-Object -ExpandProperty FullName }
    }
    if (-not $files) { Write-Host 'No .cpp files matched.'; return }
    Write-Host "clang-tidy over $($files.Count) file(s)..." -ForegroundColor DarkCyan
    # One process for all files: clang-tidy reads .clang-tidy from the repo root.
    & $tidy -p $BuildPath -quiet @files
}

function Task-Format {
    $fmt = Get-LlvmTool 'clang-format'
    if (-not (Test-Path (Join-Path $Root '.clang-format'))) {
        throw 'No .clang-format at the repo root; refusing to reformat with clang-format defaults.'
    }
    $targets = if ($Rest) { $Rest } else { @('src', 'include', 'tests', 'bench') }
    $files = $targets | ForEach-Object {
        Get-ChildItem (Join-Path $Root $_) -Recurse -Include '*.cpp', '*.hpp', '*.h' -File
    }
    Write-Host "clang-format over $($files.Count) file(s)..." -ForegroundColor DarkCyan
    & $fmt -i @($files.FullName)
}

function Task-Run {
    if (-not $Rest) { throw 'Usage: khora.ps1 run <exe-name> [args...]' }
    $exe = Join-Path $BuildPath "bin\$($Rest[0])"
    if (-not $exe.EndsWith('.exe')) { $exe += '.exe' }
    if (-not (Test-Path $exe)) { throw "No such binary: $exe. Run 'khora.ps1 build' first." }
    $exeArgs = @($Rest | Select-Object -Skip 1)
    & $exe @exeArgs
}

function Task-Clean {
    if (Test-Path $BuildPath) {
        Write-Host "Removing $BuildPath" -ForegroundColor DarkYellow
        Remove-Item -Recurse -Force $BuildPath
    }
}

# Every task below drives native tools, and Windows PowerShell turns anything a
# native exe writes to stderr into a terminating error while this is 'Stop' --
# ninja, ctest and clang-tidy all report progress on stderr. Failures are caught
# by explicit $LASTEXITCODE checks in Invoke-Checked instead.
$ErrorActionPreference = 'Continue'

switch ($Task) {
    'env'       { Import-MsvcEnv; Write-Host "MSVC ready:"; & cl 2>&1 | Select-Object -First 1; & cmake --version | Select-Object -First 1; & ninja --version }
    'configure' { Task-Configure }
    'build'     { Task-Build }
    'test'      { Task-Test; exit $script:TestExit }
    'bench'     { Task-Bench }
    'tidy'      { Task-Tidy }
    'format'    { Task-Format }
    'run'       { Task-Run }
    'clean'     { Task-Clean }
    'all'       { Task-Build; Task-Test; exit $script:TestExit }
}
