param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$download=Join-Path $root 'desktop/build/tools-downloads/esptool-4.12.0'
$output=Join-Path $root 'desktop/tools/esptool/esptool'
$zip=Join-Path $download 'esptool-v4.12.0-windows-amd64.zip'
$expected='42fddc5e6a05716868ad77fb43acbf53be041f97abed87ff850df1dc88140889'
New-Item -ItemType Directory -Path $download,$output -Force | Out-Null
if(!(Test-Path -LiteralPath $zip)) {
    Invoke-WebRequest -UseBasicParsing -Uri 'https://github.com/espressif/esptool/releases/download/v4.12.0/esptool-v4.12.0-windows-amd64.zip' -OutFile $zip
}
if((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLower() -ne $expected){throw 'esptool archive hash mismatch'}
Expand-Archive -LiteralPath $zip -DestinationPath $download -Force
foreach($file in @('esptool.exe','LICENSE','README.md')) {
    Copy-Item -LiteralPath "$download/esptool-windows-amd64/$file" -Destination "$output/$file" -Force
}
Write-Output "Prepared official esptool 4.12.0 in $output"
