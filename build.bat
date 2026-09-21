@echo off
rem Build pcmplay.exe with MSVC (64-bit).
rem Run this script from an "x64 Native Tools Command Prompt for VS 20xx",
rem or call vcvarsall.bat amd64 first.
setlocal

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo error: cl.exe not found.
    echo        Open an "x64 Native Tools Command Prompt for VS" and run
    echo        this script again, or build with CMake instead.
    exit /b 1
)

cd /d "%~dp0"
cl /nologo /O2 /W4 /D_CRT_SECURE_NO_WARNINGS ^
   /Fe:pcmplay.exe ^
   src\main.c src\audio.c src\source.c src\console.c src\util.c src\out.c ^
   /link ole32.lib
if errorlevel 1 (
    echo build failed.
    exit /b 1
)
echo build ok: pcmplay.exe
endlocal
