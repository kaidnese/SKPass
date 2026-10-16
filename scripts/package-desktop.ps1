param(
    [Parameter(Mandatory=$true)][string]$QtPrefix,
    [Parameter(Mandatory=$true)][string]$CompilerBin,
    [ValidateSet('1.0')][string]$Version='1.0'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root 'desktop/build'
$stage=Join-Path $build ('package-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))
$output=Join-Path $root "releases/v$Version/desktop"
if(!(Test-Path -LiteralPath "$build/SKPass.exe")){throw 'Build and test the desktop first'}
New-Item -ItemType Directory -Path $stage,$output -Force | Out-Null
Copy-Item -LiteralPath "$build/SKPass.exe" -Destination "$stage/SKPass-v$Version-runtime.exe"
& "$QtPrefix/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler "$stage/SKPass-v$Version-runtime.exe"
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
New-Item -ItemType Directory -Path "$stage/tools/esptool","$stage/tools/first-flash" -Force | Out-Null
foreach($file in @('esptool.exe','LICENSE','README.md')) {
    Copy-Item -LiteralPath "$root/desktop/tools/esptool/esptool/$file" -Destination "$stage/tools/esptool/$file"
}
foreach($file in @('bootloader.bin','partition-table.bin','ota_data_initial.bin')) {
    Copy-Item -LiteralPath "$root/releases/v$Version/firmware/first-flash/$file" -Destination "$stage/tools/first-flash/$file"
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip=Join-Path $build ((Split-Path $stage -Leaf)+'.zip')
[IO.Compression.ZipFile]::CreateFromDirectory($stage,$zip,[IO.Compression.CompressionLevel]::Optimal,$false)
Copy-Item -LiteralPath $zip -Destination "$stage/runtime-v$Version.zip"
Copy-Item -LiteralPath "$root/desktop/src/app.ico" -Destination "$stage/app.ico"
Push-Location $stage
try {
    & "$CompilerBin/windres.exe" "$root/desktop/src/single_file_launcher_v$Version.rc" -I . -I "$root/desktop/src" -O coff -o launcher.res.o
    if($LASTEXITCODE -ne 0){throw 'Resource compilation failed'}
    & "$CompilerBin/gcc.exe" "$root/desktop/src/single_file_launcher.c" launcher.res.o -o "$output/SKPass-v$Version.exe" -mwindows -municode -static -static-libgcc -luser32 -lshell32
    if($LASTEXITCODE -ne 0){throw 'Launcher compilation failed'}
} finally {Pop-Location}
$releaseDir=Split-Path $output -Parent
if(Test-Path -LiteralPath "$root/LICENSE"){Copy-Item -LiteralPath "$root/LICENSE" -Destination "$releaseDir/LICENSE.GPLv3.txt" -Force}
if(Test-Path -LiteralPath "$root/desktop/LICENSE.LGPLv3.txt"){Copy-Item -LiteralPath "$root/desktop/LICENSE.LGPLv3.txt" -Destination "$releaseDir/LICENSE.LGPLv3.txt" -Force}
if(Test-Path -LiteralPath "$root/THIRD_PARTY_NOTICES.md"){Copy-Item -LiteralPath "$root/THIRD_PARTY_NOTICES.md" -Destination "$releaseDir/THIRD_PARTY_NOTICES.md" -Force}
Copy-Item -LiteralPath "$root/desktop/tools/esptool/esptool/LICENSE" -Destination "$releaseDir/LICENSE.esptool.GPLv2.txt" -Force
Write-Output "Packaged: $output/SKPass-v$Version.exe"
