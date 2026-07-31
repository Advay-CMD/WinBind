rem Installs WinBind on your system.
@echo off
echo Installing WinBind...
copy /Y "WinBind.exe" "%USERPROFILE%\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup\WinBind.exe"
copy /Y "winbind.conf" "%USERPROFILE%\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup\winbind.conf"
copy /Y "base.conf" "%USERPROFILE%\AppData\Roaming\Microsoft\Windows\Start Menu\Programs\Startup\base.conf"
echo Installation complete!
