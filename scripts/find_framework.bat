@echo off
rem Sets FRAMEWORK_ROOT to the MafiaHub Framework checkout this project builds
rem against. Call this; do not run it. Exit 0 if found, 1 if not.
rem
rem Order: VANTA_FRAMEWORK, then .framework-root (written by setup.bat), then
rem "we already live at code/projects/vanta". A clone sitting on its own has
rem no framework until setup.bat puts one next to it.

setlocal EnableDelayedExpansion

set "VANTA=%~dp0.."
for %%I in ("%VANTA%") do set "VANTA=%%~fI"
set "FOUND="

if defined VANTA_FRAMEWORK (
    for %%I in ("%VANTA_FRAMEWORK%") do set "CAND=%%~fI"
    if exist "!CAND!\builds\build.bat" set "FOUND=!CAND!"
)

if not defined FOUND if exist "%VANTA%\.framework-root" (
    set /p CAND=<"%VANTA%\.framework-root"
    if exist "!CAND!\builds\build.bat" set "FOUND=!CAND!"
)

if not defined FOUND (
    for %%I in ("%VANTA%\..\..\..") do set "CAND=%%~fI"
    if exist "!CAND!\builds\build.bat" set "FOUND=!CAND!"
)

if not defined FOUND (
    endlocal
    exit /b 1
)

endlocal & set "FRAMEWORK_ROOT=%FOUND%"
exit /b 0
