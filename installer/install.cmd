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

rem An update starts right after the hub closes: give a running AeroHub up to 15 s to exit, so its files are not locked.
for /l %%i in (1,1,15) do (
    tasklist /fi "imagename eq AeroHub.exe" 2>nul | find /i "AeroHub.exe" >nul || goto extract
    ping -n 2 127.0.0.1 >nul
)

:extract
rem Files in the zip overwrite older program files; AeroHub.ini and FortsDrag.ini are not in the zip, so user data survives an update.
rem A file can still be locked for a moment (antivirus scan, the game holding FortsModule.dll): retry, then say so on screen.
set /a TRIES=0
:retry
tar -xf "%SRC%payload.zip" -C "%AEROHUB_DIR%"
if not errorlevel 1 goto extracted
set /a TRIES+=1
if %TRIES% lss 6 (
    ping -n 3 127.0.0.1 >nul
    goto retry
)
rem Error dialog in Russian; encoded so the batch file stays ASCII on any console code page.
powershell -NoProfile -EncodedCommand QQBkAGQALQBUAHkAcABlACAALQBBAHMAcwBlAG0AYgBsAHkATgBhAG0AZQAgAFAAcgBlAHMAZQBuAHQAYQB0AGkAbwBuAEYAcgBhAG0AZQB3AG8AcgBrADsAIABbAFMAeQBzAHQAZQBtAC4AVwBpAG4AZABvAHcAcwAuAE0AZQBzAHMAYQBnAGUAQgBvAHgAXQA6ADoAUwBoAG8AdwAoACcAHQQ1BCAAQwQ0BDAEOwQ+BEEETAQgAD4EMQQ9BD4EMgQ4BEIETAQgAEEAZQByAG8ASAB1AGIAOgAgAEQEMAQ5BDsESwQgAD8EQAQ+BDMEQAQwBDwEPARLBCAANwQwBD0ETwRCBEsELgAgABcEMAQ6BEAEPgQ5BEIENQQgAEEAZQByAG8ASAB1AGIAIAA4BCAAOAQzBEAEQwQgACgARgBvAHIAdABzACkAIAA4BCAANwQwBD8EQwRBBEIEOARCBDUEIABDBEEEQgQwBD0EPgQyBDoEQwQgADUESQRRBCAAQAQwBDcELgAnACwAIAAnAEEAZQByAG8ASAB1AGIAJwAsACAAJwBPAEsAJwAsACAAJwBFAHIAcgBvAHIAJwApACAAfAAgAE8AdQB0AC0ATgB1AGwAbAA=
exit /b 4

:extracted
copy /y "%SRC%uninstall.cmd" "%AEROHUB_DIR%\uninstall.cmd" >nul
if errorlevel 1 exit /b 5
if not exist "%AEROHUB_DIR%\AeroHub.exe" (
    echo AeroHub.exe was not extracted
    exit /b 6
)

rem Shortcuts in the Start menu and on the desktop. Every desktop shortcut that opens some AeroHub.exe (whatever its name,
rem for example one the user made to a copy in Downloads) is pointed at this install, so it opens the updated hub.
powershell -NoProfile -ExecutionPolicy Bypass -Command "$w=New-Object -ComObject WScript.Shell; $exe=$env:AEROHUB_DIR+'\AeroHub.exe'; $desk=[Environment]::GetFolderPath('Desktop'); foreach($d in @(($env:APPDATA+'\Microsoft\Windows\Start Menu\Programs'),$desk)){ $s=$w.CreateShortcut($d+'\'+$env:AEROHUB_NAME+'.lnk'); $s.TargetPath=$exe; $s.WorkingDirectory=$env:AEROHUB_DIR; $s.IconLocation=$exe+',0'; $s.Save() }; Get-ChildItem -LiteralPath $desk -Filter *.lnk -ErrorAction SilentlyContinue | ForEach-Object { $l=$w.CreateShortcut($_.FullName); if((Split-Path $l.TargetPath -Leaf) -ieq 'AeroHub.exe' -and $l.TargetPath -ne $exe){ $l.TargetPath=$exe; $l.WorkingDirectory=$env:AEROHUB_DIR; $l.IconLocation=$exe+',0'; $l.Save() } }"
if errorlevel 1 exit /b 7

set "KEY=HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\%AEROHUB_NAME%"
reg add "%KEY%" /v DisplayName /d "%AEROHUB_NAME%" /f >nul || exit /b 8
reg add "%KEY%" /v InstallLocation /d "%AEROHUB_DIR%" /f >nul || exit /b 8
reg add "%KEY%" /v DisplayIcon /d "%AEROHUB_DIR%\AeroHub.exe" /f >nul || exit /b 8
reg add "%KEY%" /v UninstallString /d "\"%AEROHUB_DIR%\uninstall.cmd\"" /f >nul || exit /b 8
reg add "%KEY%" /v NoModify /t REG_DWORD /d 1 /f >nul || exit /b 8
reg add "%KEY%" /v NoRepair /t REG_DWORD /d 1 /f >nul || exit /b 8

echo Installed to "%AEROHUB_DIR%"
rem Start the freshly installed hub (unless a test install asked not to).
if not defined AEROHUB_NO_START start "" "%AEROHUB_DIR%\AeroHub.exe"
exit /b 0