# Builds dist\AeroHub-Setup.exe (self-extracting, uses the iexpress tool that ships with Windows).
# Run after a Release build:  powershell -ExecutionPolicy Bypass -File installer\make-installer.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = Join-Path $root 'build\out'
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist 'stage'

# Program files only: no AeroHub.ini, FortsDrag.ini, logs or *.running-old.* (those are per-user).
$files = @('AeroHub.exe', 'modules\FortsModule.dll', 'modules\FortsModule.ini', 'profiles\forts.ini', 'profiles\minecraft.ini')
$jar = Join-Path $out 'modules\aeroclient.jar'
if (Test-Path $jar) { $files += 'modules\aeroclient.jar' }
foreach ($f in $files) { if (-not (Test-Path (Join-Path $out $f))) { throw "missing build output: $f (build Release first)" } }

if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null
$zip = Join-Path $stage 'payload.zip'
& tar.exe -a -c -f $zip -C $out @files
if ($LASTEXITCODE -ne 0) { throw "tar failed ($LASTEXITCODE)" }
Copy-Item (Join-Path $PSScriptRoot 'install.cmd') $stage
Copy-Item (Join-Path $PSScriptRoot 'uninstall.cmd') $stage

$target = Join-Path $dist 'AeroHub-Setup.exe'
if (Test-Path $target) { Remove-Item $target -Force }
$sed = Join-Path $stage 'setup.sed'
@"
[Version]
Class=IEXPRESS
SEDVersion=3
[Options]
PackagePurpose=InstallApp
ShowInstallProgramWindow=0
HideExtractAnimation=1
UseLongFileName=1
InsideCompressed=0
CAB_FixedSize=0
CAB_ResvCodeSigning=0
RebootMode=N
InstallPrompt=%InstallPrompt%
DisplayLicense=%DisplayLicense%
FinishMessage=%FinishMessage%
TargetName=%TargetName%
FriendlyName=%FriendlyName%
AppLaunched=%AppLaunched%
PostInstallCmd=%PostInstallCmd%
AdminQuietInstCmd=%AdminQuietInstCmd%
UserQuietInstCmd=%UserQuietInstCmd%
SourceFiles=SourceFiles
[Strings]
InstallPrompt=
DisplayLicense=
FinishMessage=AeroHub is installed. Start it from the Start menu.
TargetName=$target
FriendlyName=AeroHub Setup
AppLaunched=cmd /c .\install.cmd
PostInstallCmd=<None>
AdminQuietInstCmd=cmd /c .\install.cmd
UserQuietInstCmd=cmd /c .\install.cmd
FILE0="install.cmd"
FILE1="uninstall.cmd"
FILE2="payload.zip"
[SourceFiles]
SourceFiles0=$stage\
[SourceFiles0]
%FILE0%=
%FILE1%=
%FILE2%=
"@ | Set-Content -Path $sed -Encoding ascii

& "$env:WINDIR\System32\iexpress.exe" /N /Q $sed
$deadline = (Get-Date).AddSeconds(60)
while (-not (Test-Path $target) -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 500 }
if (-not (Test-Path $target)) { throw 'iexpress did not produce the installer' }
Start-Sleep -Seconds 1
$hash = (Get-FileHash $target -Algorithm SHA256).Hash
Write-Output ("built {0}  {1} bytes  SHA-256 {2}" -f $target, (Get-Item $target).Length, $hash)
