param([Parameter(Mandatory=$true)][string]$CoreRoot,[Parameter(Mandatory=$true)][string]$Version)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Copy-Item -LiteralPath (Join-Path $repo 'assets/Screencast.ico') -Destination (Join-Path $CoreRoot 'products/screencast.ico') -Force
Copy-Item -LiteralPath (Join-Path $repo 'assets/Screencast-256.png') -Destination (Join-Path $CoreRoot 'core/web/assets/icon.png') -Force
$path=Join-Path $CoreRoot 'adapters/screencast/adapter_windows.go'
$text=[IO.File]::ReadAllText($path)
$text=[regex]::Replace($text,'Screencast-shell-\d+\.\d+\.\d+\.ico',"Screencast-shell-$Version.ico")
$hook=@'
	// Refresh Explorer/Start/taskbar icon caches after replacing product artwork.
	script += `Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;public static class ScreencastIconRefresh{[DllImport("shell32.dll")] public static extern void SHChangeNotify(uint e,uint f,IntPtr a,IntPtr b);}';[ScreencastIconRefresh]::SHChangeNotify(0x08000000,0,[IntPtr]::Zero,[IntPtr]::Zero);`
	return runPS(script)
'@
if(!$text.Contains('ScreencastIconRefresh')) {
 if(([regex]::Matches($text,'return runPS\(script\)')).Count -ne 1){throw 'Pinned shortcut branding hook changed'}
 $text=$text.Replace('return runPS(script)',$hook.Trim())
}
[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'installer-branding_windows_test.go') -Destination (Join-Path $CoreRoot 'adapters/screencast/branding_windows_test.go') -Force
