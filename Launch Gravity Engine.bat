@echo off
setlocal
set "ROOT=%~dp0"
set "EXE=%ROOT%build\GravityEngine.exe"

if not exist "%EXE%" (
    echo GravityEngine.exe not found - building it first...
    cmake -S "%ROOT%." -B "%ROOT%build" -G Ninja || goto :fail
    cmake --build "%ROOT%build" || goto :fail
)

start "" "%EXE%"
exit /b 0

:fail
echo.
echo Build failed. Make sure CMake, Ninja, a C++20 compiler and raylib are installed and on PATH.
pause
exit /b 1
