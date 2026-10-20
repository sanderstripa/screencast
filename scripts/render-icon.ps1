param([string]$Destination=(Join-Path $PSScriptRoot '../assets'))
$ErrorActionPreference='Stop'
if((Resolve-Path $Destination).Path -ne (Resolve-Path (Join-Path $PSScriptRoot '../assets')).Path){throw 'Vector renderer writes to repository assets'}
& node (Join-Path $PSScriptRoot 'render-branding.cjs')
if($LASTEXITCODE){throw 'Vector branding render failed'}
