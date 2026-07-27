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

REM depot_tools ships a PATH-shadowing "ninja" file with no extension (a bash
REM script for POSIX use). CMake's find_program can match that bare name
REM before ninja.exe, which then fails to launch on Windows. Resolve the real
REM ninja.exe explicitly and hand it to CMake to avoid the ambiguity.
set "NINJA_EXE="
for /f "usebackq delims=" %%N in (`where ninja.exe 2^>nul`) do (
    if not defined NINJA_EXE set "NINJA_EXE=%%N"
)
if not defined NINJA_EXE (
    echo ninja.exe not found on PATH
    exit /b 1
)

cmake -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM="%NINJA_EXE%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

cmake --build build
if errorlevel 1 exit /b 1

echo.
echo Built: build\AutoTerminal.exe
endlocal