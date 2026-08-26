# Toolchains for differential_bench, which runs the emitted source of every
# backend against Recipe::apply.
#
# Five of the fourteen backends have a toolchain on a stock Windows box with
# node, Go, Rust and MSVC installed. Four more need nothing heavier than an npm
# install or a scaffolded project, and this fetches them into data/toolchains,
# which is gitignored -- the runner scripts beside this file are in git, the
# hundreds of megabytes they load are not.
#
# Idempotent: re-running skips anything already present.
#
#   TypeScript   the real tsc
#   C#           a console project the emitted Program.cs is dropped into
#   Lua          real Lua 5.4 compiled to WebAssembly (wasmoon)
#   Ruby         real CRuby compiled to WebAssembly (@ruby/head-wasm-wasi)
#
# Still missing after this, and each wanting a full SDK download rather than a
# package: Java, Kotlin, Swift, Haskell, PHP. The bench reports those as
# "skipped, no toolchain here", which is not a pass and is not counted as one.

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dir  = Join-Path $root 'data\toolchains'

New-Item -ItemType Directory -Force $dir | Out-Null
Push-Location $dir
try {
    if (-not (Test-Path 'package.json')) {
        Write-Host 'npm init' -ForegroundColor Cyan
        & npm init -y | Out-Null
    }

    $want = @('typescript@5', 'wasmoon', '@ruby/head-wasm-wasi', '@ruby/wasm-wasi')
    foreach ($pkg in $want) {
        $name = ($pkg -split '@(?=[^/]*$)')[0]
        if ($name -eq '') { $name = $pkg }
        if (Test-Path (Join-Path 'node_modules' $name)) {
            Write-Host "have $name" -ForegroundColor DarkGray
            continue
        }
        Write-Host "installing $pkg" -ForegroundColor Cyan
        & npm install --silent $pkg
    }

    # dotnet cannot run a bare .cs file; the bench copies the emitted source in
    # as Program.cs and builds this project.
    if (-not (Test-Path 'cs\cs.csproj')) {
        Write-Host 'scaffolding the C# project' -ForegroundColor Cyan
        New-Item -ItemType Directory -Force 'cs' | Out-Null
        & dotnet new console --force -o cs | Out-Null
    }
}
finally { Pop-Location }

Write-Host ''
Write-Host 'Ready. Verify with:' -ForegroundColor Green
Write-Host '  .\tools\khora.ps1 run differential_bench'
