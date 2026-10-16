param(
    [Parameter(Mandatory=$true)][string]$QtPrefix,
    [Parameter(Mandatory=$true)][string]$CompilerBin,
    [string]$CMake='cmake',
    [string]$Ninja='ninja',
    [string]$FirmwareBin=''
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root 'desktop/build'
if(!(Test-Path "$root/desktop/tools/esptool/esptool/esptool.exe")){& "$PSScriptRoot/prepare-esptool.ps1"}
if(!$FirmwareBin){$FirmwareBin=Join-Path $root 'releases/v1.0/firmware/SKPass.bin'}
foreach($arch in @('x86','x64')){if(!(Test-Path "$root/releases/v1.0/aimeio/$arch/aimeio_skpass.dll")){throw "Provide the $arch AimeIO release DLL first"}}
$env:PATH="$CompilerBin;$QtPrefix/bin;"+$env:PATH
& $CMake -S "$root/desktop" -B $build -G Ninja "-DCMAKE_PREFIX_PATH=$QtPrefix" "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_C_COMPILER=$CompilerBin/gcc.exe" "-DCMAKE_CXX_COMPILER=$CompilerBin/g++.exe" "-DSKPASS_FIRMWARE_BIN=$FirmwareBin" -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE -ne 0){throw 'Desktop configure failed'}
& $CMake --build $build -j4
if($LASTEXITCODE -ne 0){throw 'Desktop build failed'}
$ctest=Join-Path (Split-Path (Get-Command $CMake).Source -Parent) 'ctest.exe'
& $ctest --test-dir $build --output-on-failure
if($LASTEXITCODE -ne 0){throw 'Desktop tests failed'}
