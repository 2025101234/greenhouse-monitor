@echo off
setlocal

set "SDK_ROOT=%~dp0luban-lite-main"
set "DEFCONFIG=d12x_demo68-nor_rt-thread_helloworld_defconfig"

if not exist "%SDK_ROOT%\SConstruct" (
    echo [ERROR] SDK not found: %SDK_ROOT%
    exit /b 1
)

cd /d "%SDK_ROOT%"
set "PYTHONIOENCODING=UTF-8"
set "SDK_PRJ_TOP_DIR=%SDK_ROOT%"
set "ENV_ROOT=%SDK_ROOT%\tools\env"
set "PKGS_ROOT=%ENV_ROOT%\packages"
set "RTT_ROOT=%SDK_ROOT%\kernel\rt-thread"
set "PATH=%SDK_ROOT%\tools\env\tools\bin;%SDK_ROOT%\tools\env\tools\Python27\Scripts;%SDK_ROOT%\tools\env\tools\Python39;%SDK_ROOT%\tools\scripts\onestep;%PATH%"

if /i "%~1"=="reconfigure" (
    call "%SDK_ROOT%\tools\env\tools\Python27\Scripts\scons.bat" --apply-def=%DEFCONFIG%
    if errorlevel 1 exit /b 1
)

if not exist ".config" (
    call "%SDK_ROOT%\tools\env\tools\Python27\Scripts\scons.bat" --apply-def=%DEFCONFIG%
    if errorlevel 1 exit /b 1
)

if /i "%~1"=="clean" (
    call "%SDK_ROOT%\tools\env\tools\Python27\Scripts\scons.bat" -c
    exit /b %errorlevel%
)

call "%SDK_ROOT%\tools\env\tools\Python27\Scripts\scons.bat" -j 8
if errorlevel 1 exit /b 1

echo.
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0publish_firmware.ps1"
if errorlevel 1 exit /b 1
exit /b 0
