@echo off
set GXX=D:\gcc\mingw64\bin\g++.exe
set CFLAGS=-O2 -static -mwindows -std=c++11 -s
set LIBS=-lole32 -lgdi32 -luser32 -luuid -ldwmapi

taskkill /f /im winbind.exe >nul 2>&1
if %ERRORLEVEL%==0 echo Stopped running WinBind.exe

ping -n 2 127.0.0.1 >nul 2>&1

echo Compiling WinBind...
"%GXX%" %CFLAGS% main.cpp keybinder.cpp Config.cpp "Transparency\Transparency.cpp" "WindowStyler\WindowStyler.cpp" -o WinBind.exe %LIBS%
if %ERRORLEVEL%==0 ( echo OK ) else ( echo FAILED! & goto end )

if "%1"=="--run" (
    echo Launching WinBind...
    start /b "" WinBind.exe
    echo WinBind is running in the background.
) else (
    echo Usage: build.bat [--run]
)

:end
