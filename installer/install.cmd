@echo off
setlocal EnableExtensions
rem AeroHub per-user installer (no admin rights). Runs from the folder the self-extracting setup unpacked into.
rem AEROHUB_NAME / AEROHUB_DIR can be set beforehand to install a differently named copy (used for testing).
if not defined AEROHUB_NAME set "AEROHUB_NAME=AeroHub"
if not defined AEROHUB_DIR set "AEROHUB_DIR=%LOCALAPPDATA%\Programs\%AEROHUB_NAME%"
set "SRC=%~dp0"

if not exist "%SRC%payload.zip" (
    echo payload.zip is missing next to install.cmd
    exit /b 2
)
if not exist "%AEROHUB_DIR%" mkdir "%AEROHUB_DIR%"
if errorlevel 1 (
    echo cannot create "%AEROHUB_DIR%"
    exit /b 3
)

rem Files in the zip overwrite older program files; AeroHub.ini and FortsDrag.ini are not in the zip, so user data survives an update.
tar -xf "%SRC%payload.zip" -C "%AEROHUB_DIR%"
if errorlevel 1 (
    echo extraction failed - close AeroHub if it is running and try again
    exit /b 4
)
copy /y "%SRC%uninstall.cmd" "%AEROHUB_DIR%\uninstall.cmd" >nul
if errorlevel 1 exit /b 5
if not exist "%AEROHUB_DIR%\AeroHub.exe" (
    echo AeroHub.exe was not extracted
    exit /b 6
)

powershell -NoProfile -ExecutionPolicy Bypass -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut($env:APPDATA+'\Microsoft\Windows\Start Menu\Programs\'+$env:AEROHUB_NAME+'.lnk'); $s.TargetPath=$env:AEROHUB_DIR+'\AeroHub.exe'; $s.WorkingDirectory=$env:AEROHUB_DIR; $s.IconLocation=$env:AEROHUB_DIR+'\AeroHub.exe,0'; $s.Save()"
if errorlevel 1 exit /b 7

set "KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\%AEROHUB_NAME%"
reg add "%KEY%" /v DisplayName /d "%AEROHUB_NAME%" /f >nul || exit /b 8
reg add "%KEY%" /v InstallLocation /d "%AEROHUB_DIR%" /f >nul || exit /b 8
reg add "%KEY%" /v DisplayIcon /d "%AEROHUB_DIR%\AeroHub.exe" /f >nul || exit /b 8
reg add "%KEY%" /v UninstallString /d "\"%AEROHUB_DIR%\uninstall.cmd\"" /f >nul || exit /b 8
reg add "%KEY%" /v NoModify /t REG_DWORD /d 1 /f >nul || exit /b 8
reg add "%KEY%" /v NoRepair /t REG_DWORD /d 1 /f >nul || exit /b 8

echo Installed to "%AEROHUB_DIR%"
exit /b 0
