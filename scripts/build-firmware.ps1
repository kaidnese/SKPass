param(
    [Parameter(Mandatory=$true)][string]$IdfPath,
    [Parameter(Mandatory=$true)][string]$IdfToolsPath,
    [Parameter(Mandatory=$true)][string]$PythonEnvironment
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$env:IDF_PATH=$IdfPath
$env:IDF_TOOLS_PATH=$IdfToolsPath
$env:IDF_PYTHON_ENV_PATH=$PythonEnvironment
$env:PATH="$PythonEnvironment/Scripts;"+$env:PATH
$env:CCACHE_DISABLE='1'
. "$IdfPath/export.ps1"
$env:IDF_CCACHE_ENABLE='0'
Push-Location "$root/esp32"
try {
    & "$PythonEnvironment/Scripts/python.exe" "$IdfPath/tools/idf.py" -B build -DCCACHE_ENABLE=0 build
    if($LASTEXITCODE -ne 0){throw 'Firmware build failed'}
} finally {Pop-Location}
