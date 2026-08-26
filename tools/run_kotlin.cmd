@echo off
rem kotlinc is a batch script that needs a JDK. The one this repo fetched is the
rem only one on this host, so point JAVA_HOME at it rather than hoping.
rem   argv1 absent  -- print the compiler version, which is how the bench probes
rem   argv1 present -- compile that file and run it
setlocal
set "JAVA_HOME=%~dp0..\data\toolchains\jdk21"
set "PATH=%JAVA_HOME%\bin;%PATH%"
if "%~1"=="" (
    call "%~dp0..\data\toolchains\kotlinc\bin\kotlinc.bat" -version 2>&1
    exit /b %ERRORLEVEL%
)
call "%~dp0..\data\toolchains\kotlinc\bin\kotlinc.bat" %1 -include-runtime -d "%~dp0..\data\toolchains\kt.jar" -nowarn >nul 2>&1
if errorlevel 1 exit /b 1
"%JAVA_HOME%\bin\java.exe" -jar "%~dp0..\data\toolchains\kt.jar"
