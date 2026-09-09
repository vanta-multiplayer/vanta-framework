@echo off
rem Build Vanta from this repo. The CMake tree still lives in the framework;
rem this just finds it and forwards. From a fresh clone, run setup.bat first.
rem
rem   scripts\build.bat                 client + launcher + server + pack
rem   scripts\build.bat all             same as no args
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

if /i "%~1"=="protect" (
    powershell -NoProfile -File "%VANTA%\scripts\protect_client.ps1"
    exit /b !errorlevel!
)

if "%~1"=="" goto build_all
if /i "%~1"=="all" goto build_all
if /i "%~1"=="release" goto build_release

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
echo -- VantaMPClient (32)
call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPClient 32
if errorlevel 1 exit /b 1
echo -- VantaMPLauncher (32)
call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPLauncher 32
if errorlevel 1 exit /b 1
echo -- VantaMPServer (64)
call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPServer 64
if errorlevel 1 exit /b 1
echo -- pack ^(client + server + EOS/EAC runtime^)
powershell -NoProfile -File "%VANTA%\scripts\package_build.ps1"
if errorlevel 1 exit /b 1
rem protect is skipped by default: it re-hashes the whole client tree to
rem sign base.bin on every build, and a Debug client never starts
rem Anti-Cheat anyway (FW_DEBUG). Run "scripts\build.bat protect" on its
rem own, or "scripts\build.bat release", before shipping a protected build.
echo -- protect skipped ^(Debug^). run "scripts\build.bat protect" when shipping
exit /b 0

:build_release
echo -- VantaMPClient (32)
call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPClient 32
if errorlevel 1 exit /b 1
echo -- VantaMPLauncher (32)
call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPLauncher 32
if errorlevel 1 exit /b 1
echo -- VantaMPServer (64)
call "%FRAMEWORK_ROOT%\builds\build.bat" VantaMPServer 64
if errorlevel 1 exit /b 1
echo -- pack ^(client + server + EOS/EAC runtime^)
powershell -NoProfile -File "%VANTA%\scripts\package_build.ps1"
if errorlevel 1 exit /b 1
echo -- protect ^(base.bin - pack wipes Certificates^)
powershell -NoProfile -File "%VANTA%\scripts\protect_client.ps1"
exit /b %errorlevel%

:usage
echo usage: scripts\build.bat [target]
echo.
echo   ^(no args^) / all   client, launcher, server, then pack into build\
echo                      ^(protect skipped - Debug does not run Anti-Cheat^)
echo   release            all + protect. use before shipping
echo   client             VantaMPClient, 32-bit
echo   launcher           VantaMPLauncher, 32-bit
echo   server             VantaMPServer, 64-bit
echo   ui                 rebuild and embed the Solid page
echo   types              regenerate @vantamp/types from catalog dumps
echo   pack               copy runtime + EOSSDK / EasyAntiCheat into build\
echo   protect            run anticheat_integritytool on build\client
echo   ^<CMake target^> ^<32^|64^>
echo                      forwarded to the framework build, e.g.
echo                      scripts\build.bat VantaMPClient 32
exit /b 0
