@echo off
setlocal EnableExtensions
rem Removes the shortcut, the Apps entry and the install folder. Refuses to delete a folder that does not hold AeroHub.exe.
if not defined AEROHUB_NAME set "AEROHUB_NAME=AeroHub"
set "DIR=%~dp0"
if not exist "%DIR%AeroHub.exe" (
    echo AeroHub.exe not found next to uninstall.cmd - nothing removed
    exit /b 1
)
del /q "%APPDATA%\Microsoft\Windows\Start Menu\Programs\%AEROHUB_NAME%.lnk" 2>nul
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\%AEROHUB_NAME%" /f >nul 2>&1
rem This script lives inside the folder, so the folder is removed by a detached helper after a short delay.
start "" /b cmd /c "ping -n 3 127.0.0.1 >nul & rmdir /s /q "%DIR:~0,-1%""
exit /b 0
