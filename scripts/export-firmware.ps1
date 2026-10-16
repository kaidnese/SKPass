param([string]$BuildDir='', [ValidateSet('1.0')][string]$Version='1.0')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(!$BuildDir){$BuildDir=Join-Path $root 'esp32/build'}
$out=Join-Path $root "releases/v$Version/firmware"
New-Item -ItemType Directory -Path $out,"$out/first-flash" -Force | Out-Null
foreach($item in @(@('SKPass.bin','SKPass.bin'),@('bootloader/bootloader.bin','first-flash/bootloader.bin'),@('partition_table/partition-table.bin','first-flash/partition-table.bin'),@('ota_data_initial.bin','first-flash/ota_data_initial.bin'))){
    Copy-Item -LiteralPath (Join-Path $BuildDir $item[0]) -Destination (Join-Path $out $item[1]) -Force
}
Write-Output "Application and first-flash images exported to $out"
