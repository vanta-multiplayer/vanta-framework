@echo off
rem First-run setup for a clone or a fork.
rem
rem This repo is a framework *project*. CMake only sees it if it sits at
rem   <Framework>/code/projects/vanta
rem A developer who cloned just this repo does not have that tree yet, so
rem this script finds or clones https://github.com/MafiaHub/Framework and
rem junctions this checkout into place. Already nested under the framework
rem (the way this repo is developed) is a no-op for that step.
rem
rem   scripts\setup.bat
rem   scripts\setup.bat --framework D:\src\Framework

setlocal EnableDelayedExpansion

set "VANTA=%~dp0.."
for %%I in ("%VANTA%") do set "VANTA=%%~fI"
set "FW_URL=https://github.com/MafiaHub/Framework.git"
if defined VANTA_FRAMEWORK_URL set "FW_URL=%VANTA_FRAMEWORK_URL%"

:parse
if "%~1"=="" goto parsed
if /i "%~1"=="--framework" (
    set "VANTA_FRAMEWORK=%~2"
    shift
    shift
    goto parse
)
if /i "%~1"=="/framework" (
    set "VANTA_FRAMEWORK=%~2"
    shift
    shift
    goto parse
)
echo error: unknown argument %~1
echo usage: scripts\setup.bat [--framework ^<path^>]
exit /b 1

:parsed

echo === VantaMP setup ===
echo repo  %VANTA%
echo.

where git >nul 2>&1
if errorlevel 1 (
    echo error: git is not on PATH.
    exit /b 1
)

where python >nul 2>&1
if errorlevel 1 (
    echo error: python is not on PATH.
    exit /b 1
)

where node >nul 2>&1
if errorlevel 1 (
    echo error: node is not on PATH. The interface build needs Node.js.
    exit /b 1
)

where npm.cmd >nul 2>&1
if errorlevel 1 (
    echo error: npm is not on PATH.
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo error: Visual Studio was not found ^(vswhere.exe is missing^).
    echo        Install VS 2022 with the Desktop development with C++ workload.
    exit /b 1
)

set "VSINSTALL="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%I"
if not defined VSINSTALL (
    echo error: no Visual Studio install with the C++ toolchain was found.
    exit /b 1
)
echo vs    %VSINSTALL%

echo.
echo -- plugin-sdk
git -C "%VANTA%" submodule update --init --recursive
if errorlevel 1 (
    echo error: git submodule update failed.
    exit /b 1
)

echo.
echo -- framework
call "%~dp0find_framework.bat"
if errorlevel 1 goto need_framework
goto have_framework

:need_framework
if defined VANTA_FRAMEWORK (
    for %%I in ("%VANTA_FRAMEWORK%") do set "DEST=%%~fI"
) else (
    for %%I in ("%VANTA%\..\framework") do set "DEST=%%~fI"
)

if not exist "%DEST%\builds\build.bat" (
    if exist "%DEST%\CMakeLists.txt" (
        echo error: %DEST% exists but is not a MafiaHub Framework tree.
        exit /b 1
    )
    echo cloning %FW_URL%
    echo      to %DEST%
    git clone "%FW_URL%" "%DEST%"
    if errorlevel 1 (
        echo error: git clone of the framework failed.
        exit /b 1
    )
)

set "FRAMEWORK_ROOT=%DEST%"

:have_framework
echo fw    %FRAMEWORK_ROOT%
> "%VANTA%\.framework-root" echo %FRAMEWORK_ROOT%

set "PROJECT_SLOT=%FRAMEWORK_ROOT%\code\projects\vanta"
if /i "%PROJECT_SLOT%"=="%VANTA%" (
    echo slot  already at code/projects/vanta
    goto ui_deps
)

if exist "%PROJECT_SLOT%" (
    if exist "%PROJECT_SLOT%\scripts\setup.bat" (
        echo slot  %PROJECT_SLOT%
        goto ui_deps
    )
    echo error: %PROJECT_SLOT% already exists and is not this repo.
    echo        move it aside or pass --framework to a clean Framework checkout.
    exit /b 1
)

mkdir "%FRAMEWORK_ROOT%\code\projects" 2>nul
echo link  %PROJECT_SLOT%
mklink /J "%PROJECT_SLOT%" "%VANTA%"
if errorlevel 1 (
    echo error: could not junction this repo into the framework projects folder.
    echo        create the link by hand:
    echo        mklink /J "%PROJECT_SLOT%" "%VANTA%"
    exit /b 1
)

:ui_deps
echo.
echo -- interface packages
if exist "%VANTA%\ui\node_modules\" (
    echo npm   already installed
) else (
    pushd "%VANTA%\ui"
    call npm.cmd install
    set "NPM_ERR=!errorlevel!"
    popd
    if not "!NPM_ERR!"=="0" (
        echo error: npm install failed.
        exit /b 1
    )
)

echo.
echo Setup is done. From this repo:
echo   scripts\build.bat
echo   scripts\build.bat ui
echo.
echo Server:    %FRAMEWORK_ROOT%\builds\build-64\bin\VantaMPServer.exe
echo Launcher:  %FRAMEWORK_ROOT%\builds\build\bin\VantaMPLauncher.exe
echo.
echo You still need a legal GTA: San Andreas install to run a client.
echo Kill VantaMPLauncher, gta_sa and cef_subprocess before a client rebuild.
exit /b 0
