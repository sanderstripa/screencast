param([string]$AppDirectory = (Split-Path $PSScriptRoot -Parent))
$ErrorActionPreference = 'Stop'
$targetDirectory = Join-Path (Resolve-Path -LiteralPath $AppDirectory).Path 'models'
New-Item -ItemType Directory -Force -Path $targetDirectory | Out-Null
$modelPath = Join-Path $targetDirectory 'rvm_mobilenetv3_fp32.onnx'
$downloadPath = "$modelPath.download"
$uri = 'https://github.com/PeterL1n/RobustVideoMatting/releases/download/v1.0.0/rvm_mobilenetv3_fp32.onnx'
Invoke-WebRequest -Uri $uri -OutFile $downloadPath
if ((Get-Item -LiteralPath $downloadPath).Length -lt 1000000) { throw 'Invalid or incomplete model download' }
Move-Item -LiteralPath $downloadPath -Destination $modelPath -Force
Invoke-WebRequest -Uri 'https://raw.githubusercontent.com/PeterL1n/RobustVideoMatting/master/LICENSE' -OutFile (Join-Path $targetDirectory 'RVM-LICENSE.txt')
Write-Host "Background model installed in $targetDirectory. RVM is GPL-3.0; see THIRD_PARTY.md."
