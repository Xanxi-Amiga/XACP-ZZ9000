# Run once on Windows with an Internet connection. Subsequent MSYS builds are offline.
# Licensed upstream code is stored separately, unmodified, under third_party/Nuked-CQM/.
$ErrorActionPreference='Stop'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$dest = Join-Path $root 'third_party\Nuked-CQM'
New-Item -ItemType Directory -Path $dest -Force | Out-Null
$base='https://raw.githubusercontent.com/nukeykt/Nuked-CQM/master/'
foreach ($name in @('cqm.c','cqm.h','LICENSE')) {
    $url = $base + $name
    $out = Join-Path $dest $name
    Write-Host "Fetching $url"
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $out
    if ((Get-Item $out).Length -lt 100) { throw "Incomplete download: $name" }
}
Write-Host "Nuked-CQM source files ready: $dest"
