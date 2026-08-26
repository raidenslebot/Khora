# Toolchains for differential_bench, which runs the emitted source of all
# fourteen backends against Recipe::apply and diffs byte for byte.
#
# Five of the fourteen need nothing: a stock box with node, Go, Rust and MSVC
# covers Python, JavaScript, Go, Rust and C++. This fetches the other nine into
# data/toolchains, which is gitignored -- the runner scripts beside this file
# are in git, the four gigabytes they drive are not.
#
# Idempotent: every step checks for what it produces and skips if it is there.
#
#   TypeScript   npm      the real tsc
#   C#           dotnet   a console project the emitted Program.cs is dropped in
#   Lua          npm      real Lua 5.4 compiled to WebAssembly (wasmoon)
#   Ruby         npm      real CRuby compiled to WebAssembly (@ruby/*-wasm-wasi)
#   PHP          zip      windows.php.net
#   Java         zip      Temurin JDK 21, api.adoptium.net
#   Kotlin       zip      kotlin-compiler, github.com/JetBrains/kotlin
#   Haskell      tar.xz   GHC bindist, downloads.haskell.org
#   Swift        exe      swift.org installer, per-user into LOCALAPPDATA
#
# Nothing here is a reimplementation. Lua and Ruby have no interpreter on this
# host at all and run as the real ones compiled to wasm; everything else is the
# vendor's own toolchain. A backend checked against a mock of its own language
# is checked against nothing.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root 'data\toolchains'
$dl   = Join-Path $dir 'dl'

New-Item -ItemType Directory -Force $dir | Out-Null
New-Item -ItemType Directory -Force $dl  | Out-Null

function Step($name) { Write-Host "== $name" -ForegroundColor Cyan }
function Have($name) { Write-Host "   have $name" -ForegroundColor DarkGray }

function Fetch([string]$url, [string]$out) {
    if (Test-Path $out) { return }
    Write-Host "   downloading $(Split-Path -Leaf $out)" -ForegroundColor DarkCyan
    # curl.exe ships with Windows 10+ and is far faster than Invoke-WebRequest
    # for files this size, which matters when GHC is 310MB.
    & curl.exe -sS -L --max-time 3600 -o $out $url
    if ($LASTEXITCODE -ne 0) { throw "download failed: $url" }
}

# ---- npm packages: TypeScript, Lua, Ruby -----------------------------------
Push-Location $dir
try {
    if (-not (Test-Path 'package.json')) { & npm init -y | Out-Null }
    foreach ($pkg in @('typescript@5', 'wasmoon', '@ruby/head-wasm-wasi', '@ruby/wasm-wasi')) {
        $name = ($pkg -split '@(?=[^/]*$)')[0]
        if ($name -eq '') { $name = $pkg }
        if (Test-Path (Join-Path 'node_modules' $name)) { Have $name; continue }
        Step "npm $pkg"
        & npm install --silent $pkg
    }
}
finally { Pop-Location }

# ---- C#: dotnet will not run a bare .cs file -------------------------------
if (Test-Path (Join-Path $dir 'cs\cs.csproj')) { Have 'C# project' }
else {
    Step 'C# console project'
    New-Item -ItemType Directory -Force (Join-Path $dir 'cs') | Out-Null
    Push-Location $dir; try { & dotnet new console --force -o cs | Out-Null } finally { Pop-Location }
}

# ---- PHP -------------------------------------------------------------------
if (Test-Path (Join-Path $dir 'php\php.exe')) { Have 'PHP' }
else {
    Step 'PHP'
    # The release index names the current build; hardcoding a version rots.
    $idx = (& curl.exe -sS -L --max-time 120 'https://windows.php.net/download/')
    $zip = ([regex]::Matches($idx, 'href="(https://[^"]*/php-[0-9][^"]*-nts-Win32-vs\d+-x64\.zip)"') |
            Select-Object -First 1).Groups[1].Value
    if (-not $zip) { throw 'could not find a PHP x64 NTS zip on windows.php.net' }
    Fetch $zip (Join-Path $dl 'php.zip')
    Expand-Archive -Force -Path (Join-Path $dl 'php.zip') -DestinationPath (Join-Path $dir 'php')
}

# ---- Java: Temurin JDK 21 --------------------------------------------------
if (Test-Path (Join-Path $dir 'jdk21\bin\javac.exe')) { Have 'JDK' }
else {
    Step 'Temurin JDK 21'
    Fetch 'https://api.adoptium.net/v3/binary/latest/21/ga/windows/x64/jdk/hotspot/normal/eclipse' `
          (Join-Path $dl 'jdk.zip')
    $tmp = Join-Path $dir 'jdk_tmp'
    Expand-Archive -Force -Path (Join-Path $dl 'jdk.zip') -DestinationPath $tmp
    # The archive nests one versioned directory; flatten it to a stable name so
    # the bench does not carry a version in a path.
    Move-Item (Get-ChildItem $tmp -Directory | Select-Object -First 1).FullName (Join-Path $dir 'jdk21')
    Remove-Item -Recurse -Force $tmp
}

# ---- Kotlin (needs the JDK above) ------------------------------------------
if (Test-Path (Join-Path $dir 'kotlinc\bin\kotlinc.bat')) { Have 'Kotlin' }
else {
    Step 'Kotlin compiler'
    $tag = ([regex]::Match((& curl.exe -sS --max-time 120 'https://api.github.com/repos/JetBrains/kotlin/releases/latest'),
                           '"tag_name"\s*:\s*"(v[0-9.]+)"')).Groups[1].Value
    if (-not $tag) { throw 'could not resolve the latest Kotlin release' }
    Fetch "https://github.com/JetBrains/kotlin/releases/download/$tag/kotlin-compiler-$($tag.TrimStart('v')).zip" `
          (Join-Path $dl 'kotlin.zip')
    Expand-Archive -Force -Path (Join-Path $dl 'kotlin.zip') -DestinationPath $dir
}

# ---- Haskell: a GHC bindist, which on Windows only needs unpacking ---------
if (Test-Path (Join-Path $dir 'ghc966\bin\runghc.exe')) { Have 'GHC' }
else {
    Step 'GHC 9.6.6'
    Fetch 'https://downloads.haskell.org/~ghc/9.6.6/ghc-9.6.6-x86_64-unknown-mingw32.tar.xz' `
          (Join-Path $dl 'ghc.tar.xz')
    $tmp = Join-Path $dir 'ghc_tmp'
    New-Item -ItemType Directory -Force $tmp | Out-Null
    # Windows 10+ tar is bsdtar and reads .xz natively.
    & tar.exe -xf (Join-Path $dl 'ghc.tar.xz') -C $tmp
    Move-Item (Get-ChildItem $tmp -Directory | Select-Object -First 1).FullName (Join-Path $dir 'ghc966')
    Remove-Item -Recurse -Force $tmp
}

# ---- Swift: the vendor installer, per-user ---------------------------------
# Installs into %LOCALAPPDATA%\Programs\Swift rather than Program Files, and
# tools\_swiftenv.cmd carries the three environment variables it then needs.
if (Test-Path "$env:LOCALAPPDATA\Programs\Swift\Toolchains") { Have 'Swift' }
else {
    Step 'Swift for Windows'
    Fetch 'https://download.swift.org/swift-6.1-release/windows10/swift-6.1-RELEASE/swift-6.1-RELEASE-windows10.exe' `
          (Join-Path $dl 'swift.exe')
    & (Join-Path $dl 'swift.exe') /quiet /norestart
    Remove-Item -Force (Join-Path $dl 'swift.exe') -ErrorAction SilentlyContinue
}

Write-Host ''
Write-Host 'Ready. Verify with:' -ForegroundColor Green
Write-Host '  .\tools\khora.ps1 run differential_bench'
