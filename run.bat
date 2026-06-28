@echo off
start /b "" WinBinder.exe
echo keybinder is now running in the background.
echo Press any key to stop it...
pause >nul
taskkill /f /im WinBinder.exe >nul 2>&1
echo keybinder stopped.
