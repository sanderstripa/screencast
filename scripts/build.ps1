param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
Push-Location (Split-Path $PSScriptRoot -Parent)
try {
    cmake -S . -B build -A x64 -DBUILD_TESTING=ON
    if ($LASTEXITCODE) { throw 'CMake configuration failed' }
    cmake --build build --config $Configuration --parallel
    if ($LASTEXITCODE) { throw 'Build failed' }
    ctest --test-dir build -C $Configuration --output-on-failure
    if ($LASTEXITCODE) { throw 'Tests failed' }
    cmake --install build --config $Configuration --prefix dist/Screencast
    if ($LASTEXITCODE) { throw 'Packaging failed' }
    Copy-Item scripts dist/Screencast/scripts -Recurse -Force
    Write-Host 'Portable app: dist/Screencast/Screencast.exe'
} finally { Pop-Location }
