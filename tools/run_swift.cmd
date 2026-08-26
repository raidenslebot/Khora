@echo off
rem Swift for Windows, which needs its environment set before swiftc runs at
rem all -- see tools\_swiftenv.cmd for what and why.
rem   argv1 absent  -- print the compiler version, which is how the bench probes
rem   argv1 present -- compile that file and run it
setlocal
call "%~dp0_swiftenv.cmd"
if "%~1"=="" (
    swiftc --version 2>&1
    exit /b %ERRORLEVEL%
)
swiftc -O %1 -o "%~dp0..\sw.exe" >nul 2>&1
if errorlevel 1 exit /b 1
"%~dp0..\sw.exe"
