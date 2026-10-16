param(
    [ValidateSet('x86','x64')][string]$Architecture='x64',
    [Parameter(Mandatory=$true)][string]$CompilerBin,
    [string]$CompilerPrefix='',
    [string]$CMake='cmake',
    [string]$Ninja='ninja',
    [ValidateSet('1.0')][string]$Version='1.0'
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$build=Join-Path $root "aimeio/build-$Architecture"
$env:PATH="$CompilerBin;"+$env:PATH
$cc=Join-Path $CompilerBin ($CompilerPrefix+'gcc.exe')
$cxx=Join-Path $CompilerBin ($CompilerPrefix+'g++.exe')
if(!(Test-Path -LiteralPath $cc)) {
    $cc=Join-Path $CompilerBin ($CompilerPrefix+'clang.exe')
    $cxx=Join-Path $CompilerBin ($CompilerPrefix+'clang++.exe')
}
if(!(Test-Path -LiteralPath $cc) -or !(Test-Path -LiteralPath $cxx)){throw 'C and C++ compiler not found'}
& $CMake -S "$root/aimeio" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$Ninja" "-DCMAKE_C_COMPILER=$cc" "-DCMAKE_CXX_COMPILER=$cxx" -DCMAKE_BUILD_TYPE=Release
if($LASTEXITCODE -ne 0){throw 'AimeIO configure failed'}
& $CMake --build $build
if($LASTEXITCODE -ne 0){throw 'AimeIO build failed'}
$ctest=Join-Path (Split-Path (Get-Command $CMake).Source -Parent) 'ctest.exe'
& $ctest --test-dir $build --output-on-failure
if($LASTEXITCODE -ne 0){throw 'AimeIO tests failed'}
$dll=Join-Path $build 'aimeio_skpass.dll'
# Validate the PE machine before placing a DLL in an architecture-labelled folder.
$bytes=[IO.File]::ReadAllBytes($dll)
$pe=[BitConverter]::ToInt32($bytes,0x3c)
$machine=[BitConverter]::ToUInt16($bytes,$pe+4)
$expected=if($Architecture -eq 'x86'){0x014c}else{0x8664}
if($machine -ne $expected){throw "Compiler architecture does not match $Architecture"}
$output=Join-Path $root "releases/v$Version/aimeio/$Architecture"
New-Item -ItemType Directory -Path $output -Force | Out-Null
Copy-Item -LiteralPath $dll -Destination $output -Force
Write-Output "Built $Architecture AimeIO: $output/aimeio_skpass.dll"
