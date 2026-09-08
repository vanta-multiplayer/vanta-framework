@echo off
rem Build Vanta from this repo. The CMake tree still lives in the framework;
rem this just finds it and forwards. From a fresh clone, run setup.bat first.
rem
rem   scripts\build.bat                 client + launcher + server
rem   scripts\build.bat client
rem   scripts\build.bat launcher
rem   scripts\build.bat server
rem   scripts\build.bat ui
rem   scripts\build.bat pack
rem   scripts\build.bat VantaMPClient 32

setlocal EnableDelayedExpansion

set "VANTA=%~dp0.."
for %%I in ("%VANTA%") do set "VANTA=%%~fI"

if /i "%~1"=="help" goto usage
if /i "%~1"=="-h" goto usage
if /i "%~1"=="--help" goto usage

call "%~dp0find_framework.bat"
if errorlevel 1 (
    echo error: the MafiaHub Framework is not next to this repo.
    echo        run scripts\setup.bat first, or set VANTA_FRAMEWORK.
    exit /b 1
)

if /i "%~1"=="ui" (
    python "%VANTA%\scripts\build_ui.py" --skip-install
    exit /b !errorlevel!
)

if /i "%~1"=="types" (
    python "%VANTA%\scripts\generate_scripting_types.py"
    exit /b !errorlevel!
)

if /i "%~1"=="pack" (
    powershell -NoProfile -File "%VANTA%\scripts\package_build.ps1"
    exit /b !errorlevel!
)

if "%~1"=="" goto build_all
if /i "%~1"=="all" goto build_all

if /i "%~1"=="client" (
    call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPClient 32
    exit /b !errorlevel!
)

if /i "%~1"=="launcher" (
    call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPLauncher 32
    exit /b !errorlevel!
)

if /i "%~1"=="server" (
    call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPServer 64
    exit /b !errorlevel!
)

call "%FRAMEWORK_ROOT%\builds\build.bat" %*
exit /b %errorlevel%

:build_all
call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPClient 32
if errorlevel 1 exit /b 1
call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPLauncher 32
if errorlevel 1 exit /b 1
call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPServer 64
exit /b %errorlevel%

:usage
echo usage: scripts\build.bat [target]
echo.
echo   ^(no args^) / all   client, launcher, server
echo   client             VantaMPClient, 32-bit
echo   launcher           VantaMPLauncher, 32-bit
echo   server             VantaMPServer, 64-bit
echo   ui                 rebuild and embed the Solid page
echo   types              regenerate @vantamp/types from catalog dumps
echo   pack               copy runtime files into a tester tree
echo   ^<CMake target^> ^<32^|64^>
echo                      forwarded to the framework build, e.g.
echo                      scripts\build.bat VantaMPClient 32
exit /b 0
