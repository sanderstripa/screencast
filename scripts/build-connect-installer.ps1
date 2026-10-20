param(
 [Parameter(Mandatory=$true)][string]$PayloadPath,
 [Parameter(Mandatory=$true)][string]$Version,
 [Parameter(Mandatory=$true)][string]$OutFile,
 [string]$ExtensionPath,[string]$ExpectedExecutable,[string]$CoreRoot,[string]$Go='go',
 [switch]$TestUI
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(!$CoreRoot){
 $localCore=Join-Path $repo 'installer-core'
 if(Test-Path (Join-Path $localCore 'build.ps1')){$CoreRoot=$localCore}
 else{
  $pin=Get-Content (Join-Path $repo 'installer-core.json') -Raw | ConvertFrom-Json
  if($pin.repository -ne 'sanderstripa/connect' -or $pin.commit -notmatch '^[a-f0-9]{40}$'){throw 'Invalid shared installer pin'}
  $checkout=Join-Path $repo '.connect-installer-source'
  if(!(Test-Path (Join-Path $checkout '.git'))){
   git init $checkout
   if($LASTEXITCODE){throw 'Core checkout initialization failed'}
   git -C $checkout remote add origin "https://github.com/$($pin.repository).git"
   if($LASTEXITCODE){throw 'Core remote setup failed'}
  }
  $current=git -C $checkout rev-parse HEAD 2>$null
  if($current -ne $pin.commit){
   if(git -C $checkout status --porcelain --untracked-files=no){throw 'Shared core has local source changes'}
   git -C $checkout fetch --depth 1 origin $pin.commit
   if($LASTEXITCODE){throw 'Pinned installer core fetch failed'}
   git -C $checkout checkout --detach $pin.commit
   if($LASTEXITCODE){throw 'Pinned installer core checkout failed'}
  }
  if((git -C $checkout rev-parse HEAD) -ne $pin.commit){throw 'Installer core revision mismatch'}
  $CoreRoot=Join-Path $checkout 'installer-core'
 }
}
$core=(Resolve-Path -LiteralPath $CoreRoot).Path
# Product branding override only; keep the pinned shared installer behavior and UI intact.
Copy-Item -LiteralPath (Join-Path $repo 'assets/Screencast.ico') -Destination (Join-Path $core 'products/screencast.ico') -Force
Copy-Item -LiteralPath (Join-Path $repo 'assets/Screencast-256.png') -Destination (Join-Path $core 'core/web/assets/icon.png') -Force
$adapterPath=Join-Path $core 'adapters/screencast/adapter_windows.go'
$adapterText=[IO.File]::ReadAllText($adapterPath)
$adapterText=$adapterText.Replace('Screencast-shell-0.2.6.ico','Screencast-shell-0.4.3.ico')
[IO.File]::WriteAllText($adapterPath,$adapterText,[Text.UTF8Encoding]::new($false))
$sourceCommit=(git -C $repo rev-parse HEAD).Trim()
& (Join-Path $core 'build.ps1') -Product 'screencast' -PayloadPath $PayloadPath -Version $Version -OutFile $OutFile -ExtensionPath $ExtensionPath -ExpectedExecutable $ExpectedExecutable -ProductCommit $sourceCommit -Go $Go
if($LASTEXITCODE){throw 'Shared installer build failed'}
if($TestUI){& (Join-Path $core 'test-ui.ps1') -OutputDirectory (Join-Path $repo '.tmp/installer-review')}
