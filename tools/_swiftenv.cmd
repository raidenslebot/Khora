@echo off
rem Swift for Windows needs four things set before swiftc will build anything:
rem   MSVC env  vcvars64, for the C++ toolchain the Swift linker drives
rem   PATH      the Runtimes bin, or every invocation dies with 0xC0000135
rem   SDKROOT   Swift ships its OWN Windows.sdk; without it the compiler says
rem             "unable to load standard library for target"
rem   LIB       the real Windows SDK libraries. vcvars64 on this host leaves
rem             INCLUDE and LIB without ucrt/um because the SDK is on disk and
rem             not registered with a VS Installer that was removed -- the same
rem             gap khora.ps1 documents and works around in Import-WindowsSdk.
rem             Without it: LNK1104: cannot open file 'kernel32.lib'.
call "C:\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1

set "SW=%LOCALAPPDATA%\Programs\Swift"
set "SWVER=6.3.3"
set "PATH=%SW%\Runtimes\%SWVER%\usr\bin;%SW%\Toolchains\%SWVER%+Asserts\usr\bin;%PATH%"
set "SDKROOT=%SW%\Platforms\%SWVER%\Windows.platform\Developer\SDKs\Windows.sdk"

set "WK=C:\Program Files (x86)\Windows Kits\10"
for /f "delims=" %%v in ('dir /b /on "%WK%\Lib" 2^>nul') do (
    if exist "%WK%\Lib\%%v\um\x64\kernel32.Lib" set "WKV=%%v"
)
if defined WKV (
    set "LIB=%WK%\Lib\%WKV%\ucrt\x64;%WK%\Lib\%WKV%\um\x64;%LIB%"
    set "INCLUDE=%WK%\Include\%WKV%\ucrt;%WK%\Include\%WKV%\um;%WK%\Include\%WKV%\shared;%INCLUDE%"
)
