@echo off
start /b "" keybinder.exe
echo keybinder is now running in the background.
echo Press any key to stop it...
pause >nul
taskkill /f /im keybinder.exe >nul 2>&1
echo keybinder stopped.
