@echo off
REM Build AutoTerminal with MSVC + Ninja.
REM Usage: build.bat [clean]

setlocal
cd /d "%~dp0"

if /I "%1"=="clean" (
    if exist build rmdir /S /Q build
)

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build build
if errorlevel 1 exit /b 1

echo.
echo Built: build\AutoTerminal.exe
endlocal